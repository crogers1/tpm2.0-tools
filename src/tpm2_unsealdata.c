//**********************************************************************;
// Copyright (c) 2015, Intel Corporation
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// 3. Neither the name of Intel Corporation nor the names of its contributors
// may be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
// THE POSSIBILITY OF SUCH DAMAGE.
//**********************************************************************;

#include <stdarg.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <getopt.h>

#include <sapi/tpm20.h>
#include "sample.h"
#include <tcti/tcti_socket.h>
#include "common.h"

int debugLevel = 0;
TPMS_AUTH_COMMAND sessionData;
int hexPasswd = false;
TPM_HANDLE handle2048rsa;
TPM_HANDLE primaryHandle;

int setAlg(TPMI_ALG_PUBLIC type,TPMI_ALG_HASH nameAlg,TPM2B_PUBLIC *inPublic)
{
    switch(nameAlg)
    {
    case TPM_ALG_SHA1:
    case TPM_ALG_SHA256:
    case TPM_ALG_SHA384:
    case TPM_ALG_SHA512:
    case TPM_ALG_SM3_256:
    case TPM_ALG_NULL:
        inPublic->t.publicArea.nameAlg = nameAlg;
        break;
    default:
        printf("nameAlg algrithm: 0x%0x not support !\n", nameAlg);
        return -1;
    }

    // First clear attributes bit field.
    *(UINT32 *)&(inPublic->t.publicArea.objectAttributes) = 0;
    inPublic->t.publicArea.objectAttributes.restricted = 1;
    inPublic->t.publicArea.objectAttributes.userWithAuth = 1;
    inPublic->t.publicArea.objectAttributes.decrypt = 1;
    inPublic->t.publicArea.objectAttributes.fixedTPM = 1;
    inPublic->t.publicArea.objectAttributes.fixedParent = 1;
    inPublic->t.publicArea.objectAttributes.sensitiveDataOrigin = 1;
    inPublic->t.publicArea.authPolicy.t.size = 0;

    inPublic->t.publicArea.type = type;
    switch(type)
    {
    case TPM_ALG_RSA:
        inPublic->t.publicArea.parameters.rsaDetail.symmetric.algorithm = TPM_ALG_AES;
        inPublic->t.publicArea.parameters.rsaDetail.symmetric.keyBits.aes = 128;
        inPublic->t.publicArea.parameters.rsaDetail.symmetric.mode.aes = TPM_ALG_CFB;
        inPublic->t.publicArea.parameters.rsaDetail.scheme.scheme = TPM_ALG_NULL;
        inPublic->t.publicArea.parameters.rsaDetail.keyBits = 2048;
        inPublic->t.publicArea.parameters.rsaDetail.exponent = 0;
        inPublic->t.publicArea.unique.rsa.t.size = 0;
        break;

    case TPM_ALG_KEYEDHASH:
        inPublic->t.publicArea.parameters.keyedHashDetail.scheme.scheme = TPM_ALG_XOR;
        inPublic->t.publicArea.parameters.keyedHashDetail.scheme.details.exclusiveOr.hashAlg = TPM_ALG_SHA256;
        inPublic->t.publicArea.parameters.keyedHashDetail.scheme.details.exclusiveOr.kdf = TPM_ALG_KDF1_SP800_108;
        inPublic->t.publicArea.unique.keyedHash.t.size = 0;
        break;

    case TPM_ALG_ECC:
        inPublic->t.publicArea.parameters.eccDetail.symmetric.algorithm = TPM_ALG_AES;
        inPublic->t.publicArea.parameters.eccDetail.symmetric.keyBits.aes = 128;
        inPublic->t.publicArea.parameters.eccDetail.symmetric.mode.sym = TPM_ALG_CFB;
        inPublic->t.publicArea.parameters.eccDetail.scheme.scheme = TPM_ALG_NULL;
        inPublic->t.publicArea.parameters.eccDetail.curveID = TPM_ECC_NIST_P256;
        inPublic->t.publicArea.parameters.eccDetail.kdf.scheme = TPM_ALG_NULL;
        inPublic->t.publicArea.unique.ecc.x.t.size = 0;
        inPublic->t.publicArea.unique.ecc.y.t.size = 0;
        break;

    case TPM_ALG_SYMCIPHER:
        inPublic->t.publicArea.parameters.symDetail.sym.algorithm = TPM_ALG_AES;
        inPublic->t.publicArea.parameters.symDetail.sym.keyBits.sym = 128;
        inPublic->t.publicArea.parameters.symDetail.sym.mode.sym = TPM_ALG_CFB;
        inPublic->t.publicArea.unique.sym.t.size = 0;
        break;

    default:
        printf("type algrithm: 0x%0x not support !\n",type);
        return -2;
    }
    return 0;
}

TPM_RC CreatePrimary(TPMI_RH_HIERARCHY hierarchy, TPM2B_PUBLIC *inPublic, TPMI_ALG_PUBLIC type, TPMI_ALG_HASH nameAlg, int P_flag)
{
	TPM_RC rval;
	TPMS_AUTH_RESPONSE sessionDataOut;
	TSS2_SYS_CMD_AUTHS sessionsData;
	TSS2_SYS_RSP_AUTHS sessionsDataOut;
	TPMS_AUTH_COMMAND *sessionDataArray[1];
	TPMS_AUTH_RESPONSE *sessionDataOutArray[1];

	TPM2B_DATA              outsideInfo = { { 0, } };
	TPML_PCR_SELECTION      creationPCR;
	TPM2B_NAME              name = { { sizeof(TPM2B_NAME)-2, } };
	TPM2B_PUBLIC            outPublic = { { 0, } };
	TPM2B_CREATION_DATA     creationData = { { 0, } };
	TPM2B_DIGEST            creationHash = { { sizeof(TPM2B_DIGEST)-2, } };
	TPMT_TK_CREATION        creationTicket = { 0, };
	TPM2B_SENSITIVE_CREATE inSensitive;

	sessionDataArray[0] = &sessionData;
	sessionDataOutArray[0] = &sessionDataOut;

	sessionsDataOut.rspAuths = &sessionDataOutArray[0];
	sessionsData.cmdAuths = &sessionDataArray[0];

	sessionsData.cmdAuthsCount = 1;
	sessionsDataOut.rspAuthsCount = 1;

	sessionData.sessionHandle = TPM_RS_PW;
	sessionData.nonce.t.size = 0;

	if(P_flag == 0)
		sessionData.hmac.t.size = 0;
	
	*((UINT8 *)((void *)&sessionData.sessionAttributes)) = 0;
	
	if (sessionData.hmac.t.size > 0 && hexPasswd)
	{
		sessionData.hmac.t.size = sizeof(sessionData.hmac) - 2;
		if (hex2ByteStructure((char *)sessionData.hmac.t.buffer,
							  &sessionData.hmac.t.size,
							  sessionData.hmac.t.buffer) != 0)
		{
			printf( "Failed to convert Hex format password for hierarchy Passwd.\n");
			return -1;
		}
	}

	//remove this line if uncommenting above block
	inSensitive.t.sensitive.userAuth.t.size = 0;

	inSensitive.t.sensitive.data.t.size = 0;
	inSensitive.t.size = inSensitive.t.sensitive.userAuth.b.size + 2;

    if(setAlg(type, nameAlg, inPublic))
        return -1;

    creationPCR.count = 0;

    rval = Tss2_Sys_CreatePrimary(sysContext, hierarchy, &sessionsData, &inSensitive, inPublic, &outsideInfo, &creationPCR, &primaryHandle, &outPublic, &creationData, &creationHash, &creationTicket, &name, &sessionsDataOut);
    if(rval != TPM_RC_SUCCESS)
    {
        printf("\nCreatePrimary Failed ! ErrorCode: 0x%0x\n\n",rval);
        return -2;
    }
    printf("\nCreatePrimary Succeed ! Handle: 0x%8.8x\n\n",primaryHandle);

	return 0;
}


UINT32 unseal(TPMI_DH_OBJECT itemHandle, const char *outFileName, int P_flag, TPM2B_PUBLIC *inPublic, TPM2B_PRIVATE *inPrivate, TPMI_ALG_HASH nameAlg, 
				UINT32 *pcrList, UINT32 pcrCount, TPMI_RH_HIERARCHY hierarchy, int A_flag)
{
    UINT32 rval;
	SESSION *policySession;
    TPMS_AUTH_RESPONSE sessionDataOut;
    TSS2_SYS_CMD_AUTHS sessionsData;
    TSS2_SYS_RSP_AUTHS sessionsDataOut;
    TPMS_AUTH_COMMAND *sessionDataArray[1];
    TPMS_AUTH_RESPONSE *sessionDataOutArray[1];
	TPM2B_DIGEST policyDigest; //unused for now here but BuildPolicyExternal needs to return the policy for sealdata.
	TPM2B_PUBLIC tempPublic;

    TPM2B_NAME nameExt = { { sizeof(TPM2B_NAME)-2, } };

	rval = BuildPolicyExternal(sysContext, &policySession, false, pcrList, pcrCount, &policyDigest, nameAlg);  //Build real policy, don't write to file
	if(rval != TPM_RC_SUCCESS)
	{
		printf("BuildPolicy failed, errorcode: 0x%x\n", rval);
		return rval;
	}

	//Create the parent context
	if(A_flag)
	{
		rval = CreatePrimary(hierarchy, &tempPublic, TPM_ALG_RSA, nameAlg, P_flag); 
		if(rval != TPM_RC_SUCCESS)
		{
			printf("CreatePrimary failed, errorcode: 0x%x\n", rval);
			return rval;
		}
	}

    TPM2B_SENSITIVE_DATA outData = {{sizeof(TPM2B_SENSITIVE_DATA)-2, }};

    sessionDataArray[0] = &sessionData;
    sessionDataOutArray[0] = &sessionDataOut;

    sessionsDataOut.rspAuths = &sessionDataOutArray[0];
    sessionsData.cmdAuths = &sessionDataArray[0];

    sessionsDataOut.rspAuthsCount = 1;
    sessionsData.cmdAuthsCount = 1;

    sessionData.sessionHandle = TPM_RS_PW;
    sessionData.nonce.t.size = 0;
    *((UINT8 *)((void *)&sessionData.sessionAttributes)) = 0;
	sessionData.sessionAttributes.continueSession = 1;
    if(P_flag == 0)
        sessionData.hmac.t.size = 0;
    if (sessionData.hmac.t.size > 0 && hexPasswd)
    {
        sessionData.hmac.t.size = sizeof(sessionData.hmac) - 2;
        if (hex2ByteStructure((char *)sessionData.hmac.t.buffer,
                              &sessionData.hmac.t.size,
                              sessionData.hmac.t.buffer) != 0)
        {
            printf( "Failed to convert Hex format password for item Passwd.\n");
            return -1;
        }
    }

	//Use the handle we just created from CreatePrimary
	//Otherwise, use the provided handle
	if(A_flag)
    	rval = Tss2_Sys_Load(sysContext, primaryHandle, &sessionsData, inPrivate , inPublic, &handle2048rsa, &nameExt, &sessionsDataOut);
	else
	    rval = Tss2_Sys_Load(sysContext, itemHandle, &sessionsData, inPrivate , inPublic, &handle2048rsa, &nameExt, &sessionsDataOut);
    if(rval != TPM_RC_SUCCESS)
    {
        printf("\nLoad Object Failed ! ErrorCode: 0x%0x\n\n",rval);
        return -1;
    }

    sessionData.sessionHandle = policySession->sessionHandle;
    rval = Tss2_Sys_Unseal(sysContext, handle2048rsa, &sessionsData, &outData, &sessionsDataOut);
    if(rval != TPM_RC_SUCCESS)
    {
        printf("Unseal failed. Error Code: 0x%x\n", rval);
        return -1;
    }

    printf("\nUnseal succ.\nUnsealed data: ");
    for(UINT16 i = 0; i < outData.t.size; i++)
        printf(" 0x%02x", outData.t.buffer[i]);
    printf("\n");

    if(saveDataToFile(outFileName, (UINT8 *)outData.t.buffer, outData.t.size))
    {
        printf("Failed to save unsealed data into %s\n", outFileName);
        return -2;
    }

	//Now clean up our session
	rval = Tss2_Sys_FlushContext( sysContext, policySession->sessionHandle );	
	if(rval != TPM_RC_SUCCESS)
	{
		printf("FlushContext failed: Error Code: -x%x\n", rval);
		return -3;
	}

	rval = EndAuthSession( policySession );
	if(rval != TPM_RC_SUCCESS)
	{
		printf("EndAuthSession failed: Error Code: -x%x\n", rval);
		return -4;
	}

    return 0;
}

void showHelp(const char *name)
{
    printf("\n%s  [options]\n"
        "\n"
        "-h, --help               Display command tool usage info;\n"
        "-v, --version            Display command tool version info\n"
        "-A, --auth <o | p |...>  the authorization used to authorize thecommands\n"
            "\to  TPM_RH_OWNER\n"
            "\tp  TPM_RH_PLATFORM\n"
            "\te  TPM_RH_ENDORSEMENT\n"
            "\tn  TPM_RH_NULL\n"
        "-H, --item    <itemHandle>     item handle, handle of a loaded data object\n"
        "-c, --itemContext <filename>   filename for item context\n"
        "-P, --pwdi    <itemPassword>   item handle password, optional\n"
        "-u, --pubfile   <publicKeyFileName>   The public portion of the object\n"
        "-n, --privfile  <privateKeyFileName>  The sensitive portion of the object\n"
        "-C, --context <filename>   The file to save the object context, optional"
        "-o, --outfile <outPutFilename> Output file name, containing the unsealed data\n"
        "-X, --passwdInHex              passwords given by any options are hex format.\n"
        "-p, --port  <port number>  The Port number, default is %d, optional\n"
        "-r, --pcr  <pcrID>  The PCR used in the gated policy\n"
        "-g, --halg   <hexAlg>  algorithm used for computing the Name of the object\n"
            "\t0x0004  TPM_ALG_SHA1\n"
            "\t0x000B  TPM_ALG_SHA256\n"
            "\t0x000C  TPM_ALG_SHA384\n"
            "\t0x000D  TPM_ALG_SHA512\n"
            "\t0x0012  TPM_ALG_SM3_256\n"
        "-d, --debugLevel <0|1|2|3> The level of debug message, default is 0, optional\n"
            "\t0 (high level test results)\n"
            "\t1 (test app send/receive byte streams)\n"
            "\t2 (resource manager send/receive byte streams)\n"
            "\t3 (resource manager tables)\n"
        "\n"
        "Example:\n"
        "%s -H 0x80000000 -P abc123 -o <outPutFileName>\n"
        "%s -H 0x80000000 -o <outPutFileName>\n\n"// -i <simulator IP>\n\n",DEFAULT_TPM_PORT);
        "%s -H 0x80000000 -P 123abc -X -o <outPutFileName>\n"
        ,name, DEFAULT_RESMGR_TPM_PORT, name, name, name);
}

int main(int argc, char* argv[])
{
    char hostName[200] = DEFAULT_HOSTNAME;
    int port = DEFAULT_RESMGR_TPM_PORT;
	
	TPMI_ALG_HASH nameAlg;
    TPMI_DH_OBJECT itemHandle, parentHandle;
    TPM2B_PUBLIC  inPublic;
    TPM2B_PRIVATE inPrivate;
    char outFilePath[PATH_MAX] = {0};
    char *contextItemFile = NULL;
    char *contextLoadFile = NULL;
    TPMI_RH_HIERARCHY hierarchy = TPM_RH_NULL;
	UINT16 size;
	UINT32 pcr = -1;
	UINT32 pcrCount = 0;
	UINT32 pcrList[24];

    setbuf(stdout, NULL);
    setvbuf (stdout, NULL, _IONBF, BUFSIZ);

    memset(&inPublic,0,sizeof(TPM2B_PUBLIC));
    memset(&inPrivate,0,sizeof(TPM2B_SENSITIVE));
    int opt = -1;
    const char *optstring = "hvH:P:o:p:d:c:r:u:C:g:n:A:X";
    static struct option long_options[] = {
      {"help",0,NULL,'h'},
      {"version",0,NULL,'v'},
      {"item",1,NULL,'H'},
      {"pwdi",1,NULL,'P'},
      {"auth",1,NULL,'A'},
      {"outfile",1,NULL,'o'},
      {"pubfile",1,NULL,'u'},
      {"privfile",1,NULL,'n'},
      {"port",1,NULL,'p'},
      {"debugLevel",1,NULL,'d'},
      {"itemContext",1,NULL,'c'},
      {"halg",1,NULL,'g'},
      {"pcr",1,NULL,'r'},
      {"loadContext",1,NULL,'C'},
      {"passwdInHex",0,NULL,'X'},
      {0,0,0,0}
    };

    int returnVal = 0;
    int flagCnt = 0;
    int h_flag = 0,
        v_flag = 0,
        H_flag = 0,
        P_flag = 0,
        c_flag = 0,
        C_flag = 0,
        u_flag = 0,
        r_flag = 0,
        g_flag = 0,
        n_flag = 0,
        A_flag = 0,
        o_flag = 0;

    if(argc == 1)
    {
        showHelp(argv[0]);
        return 0;
    }

    while((opt = getopt_long(argc,argv,optstring,long_options,NULL)) != -1)
    {
        switch(opt)
        {
        case 'h':
            h_flag = 1;
            break;
        case 'v':
            v_flag = 1;
            break;
		case 'A':
            if(strcmp(optarg,"o") == 0 || strcmp(optarg,"O") == 0)
                hierarchy = TPM_RH_OWNER;
            else if(strcmp(optarg,"p") == 0 || strcmp(optarg,"P") == 0)
                hierarchy = TPM_RH_PLATFORM;
            else if(strcmp(optarg,"e") == 0 || strcmp(optarg,"E") == 0)
                hierarchy = TPM_RH_ENDORSEMENT;
            else if(strcmp(optarg,"n") == 0 || strcmp(optarg,"N") == 0)
                hierarchy = TPM_RH_NULL;
            else
            {
                returnVal = -19;
                break;
            }
            A_flag = 1;
            break;
        case 'H':
            if(getSizeUint32Hex(optarg, &itemHandle) != 0)
            {
                returnVal = -1;
                break;
            }
            printf("\nitemHandle: 0x%x\n\n",itemHandle);
            H_flag = 1;
            break;
        case 'P':
            sessionData.hmac.t.size = sizeof(sessionData.hmac.t) - 2;
            if(str2ByteStructure(optarg,&sessionData.hmac.t.size,sessionData.hmac.t.buffer) != 0)
            {
                returnVal = -2;
                break;
            }
            P_flag = 1;
            break;
        case 'o':
            safeStrNCpy(outFilePath, optarg, sizeof(outFilePath));
            if(checkOutFile(outFilePath) != 0)
            {
                returnVal = -3;
                break;
            }
            o_flag = 1;
            break;
        case 'p':
            if( getPort(optarg, &port) )
            {
                printf("Incorrect port number.\n");
                returnVal = -4;
            }
            break;
        case 'd':
            if( getDebugLevel(optarg, &debugLevel) )
            {
                printf("Incorrect debug level.\n");
                returnVal = -5;
            }
            break;
        case 'c':
            contextItemFile = optarg;
            if(contextItemFile == NULL || contextItemFile[0] == '\0')
            {
                returnVal = -6;
                break;
            }
            printf("contextItemFile = %s\n", contextItemFile);
            c_flag = 1;
            break;
        case 'C':
            contextLoadFile = optarg;
            if(contextLoadFile == NULL || contextLoadFile[0] == '\0')
            {
                returnVal = -6;
                break;
            }
            printf("contextLoadFile = %s\n", contextLoadFile);
            C_flag = 1;
			break;
        case 'u':
            size = sizeof(inPublic);
			printf("inPublic: %s\n", optarg);
            if(loadDataFromFile(optarg, (UINT8 *)&inPublic, &size) != 0)
            {
                returnVal = -3;
                break;
            }
            u_flag = 1;
            break;
        case 'n':
            size = sizeof(inPrivate);
			printf("inPrivate: %s\n", optarg);
            if(loadDataFromFile(optarg, (UINT8 *)&inPrivate, &size) != 0)
            {
                returnVal = -4;
                break;
            }
            n_flag = 1;
            break;
        case 'g':
            if(getSizeUint16Hex(optarg,&nameAlg) != 0)
            {
                showArgError(optarg, argv[0]);
                returnVal = -4;
                break;
            }
            printf("nameAlg = 0x%4.4x\n", nameAlg);
            g_flag = 1;
            break;
		case 'r':
			if ( getPcrId(optarg, &pcr) )
			{
				printf("Invalid pcr value.\n");
				returnVal = -7;
			}
			r_flag = 1;
			pcrList[pcrCount] = pcr;
			pcrCount++;
			break;
        case 'X':
            hexPasswd = true;
            break;
        case ':':
//              printf("Argument %c needs a value!\n",optopt);
            returnVal = -7;
            break;
        case '?':
//              printf("Unknown Argument: %c\n",optopt);
            returnVal = -8;
            break;
        //default:
        //  break;
        }
        if(returnVal)
            break;
    };

    if(returnVal != 0)
        return returnVal;

    flagCnt = h_flag + v_flag + H_flag + o_flag + c_flag + n_flag + u_flag + g_flag + r_flag;
    if(flagCnt == 1)
    {
        if(h_flag == 1)
            showHelp(argv[0]);
        else if(v_flag == 1)
            showVersion(argv[0]);
        else
        {
            showArgMismatch(argv[0]);
            return -9;
        }
    }
    else if(flagCnt >= 5 && (H_flag == 1 || c_flag ==1 || A_flag == 1) && o_flag == 1 && n_flag == 1 && u_flag == 1 && r_flag == 1)
    {
        prepareTest(hostName, port, debugLevel);
		
        if(c_flag && checkOutFile(contextItemFile))
            returnVal = loadTpmContextFromFile(sysContext, itemHandle, contextItemFile );
        if (returnVal == 0)
            returnVal = unseal(itemHandle, outFilePath, P_flag, &inPublic, &inPrivate, nameAlg, pcrList, pcrCount, hierarchy, A_flag);
        if (returnVal == 0 && C_flag)
			returnVal = saveTpmContextToFile(sysContext, handle2048rsa, contextLoadFile); 

        finishTest();

        if(returnVal)
            return -10;
    }
    else
    {
        showArgMismatch(argv[0]);
        return -11;
    }

    return 0;
}
