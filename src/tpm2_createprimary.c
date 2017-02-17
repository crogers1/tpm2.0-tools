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
#include <stdbool.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <getopt.h>

#include <sapi/tpm20.h>
#include <tcti/tcti_socket.h>

#include "main.h"
#include "options.h"
#include "string-bytes.h"

TPMS_AUTH_COMMAND sessionData;
bool hexPasswd = false;
TPM_HANDLE handle2048rsa;

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

int createPrimary(TSS2_SYS_CONTEXT *sysContext, TPMI_RH_HIERARCHY hierarchy, TPM2B_PUBLIC *inPublic, TPM2B_SENSITIVE_CREATE *inSensitive, TPMI_ALG_PUBLIC type, TPMI_ALG_HASH nameAlg, int P_flag, int K_flag)
{
    UINT32 rval;
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

    if(K_flag == 0)
        inSensitive->t.sensitive.userAuth.t.size = 0;
    if (inSensitive->t.sensitive.userAuth.t.size > 0 && hexPasswd)
    {
        inSensitive->t.sensitive.userAuth.t.size = sizeof(inSensitive->t.sensitive.userAuth) - 2;
        if (hex2ByteStructure((char *)inSensitive->t.sensitive.userAuth.t.buffer,
                              &inSensitive->t.sensitive.userAuth.t.size,
                              inSensitive->t.sensitive.userAuth.t.buffer) != 0)
        {
            printf( "Failed to convert Hex format password for primary Passwd.\n");
            return -1;
        }
    }

    inSensitive->t.sensitive.data.t.size = 0;
    inSensitive->t.size = inSensitive->t.sensitive.userAuth.b.size + 2;

    if(setAlg(type, nameAlg, inPublic))
        return -1;

    creationPCR.count = 0;

    rval = Tss2_Sys_CreatePrimary(sysContext, hierarchy, &sessionsData, inSensitive, inPublic,  &outsideInfo, &creationPCR, &handle2048rsa, &outPublic, &creationData, &creationHash, &creationTicket, &name, &sessionsDataOut);
    if(rval != TPM_RC_SUCCESS)
    {
        printf("\nCreatePrimary Failed ! ErrorCode: 0x%0x\n\n",rval);
        return -2;
    }
    printf("\nCreatePrimary Succeed ! Handle: 0x%8.8x\n\n",handle2048rsa);
    return 0;
}

int
execute_tool (int               argc,
              char             *argv[],
              char             *envp[],
              common_opts_t    *opts,
              TSS2_SYS_CONTEXT *sapi_context)
{

    char hostName[200] = DEFAULT_HOSTNAME;
	char pass[40];
	char *index = NULL;

    TPM2B_SENSITIVE_CREATE  inSensitive;
    TPM2B_PUBLIC            inPublic;
    TPMI_ALG_PUBLIC type;
    TPMI_ALG_HASH nameAlg;
    TPMI_RH_HIERARCHY hierarchy = TPM_RH_NULL;

    setbuf(stdout, NULL);
    setvbuf (stdout, NULL, _IONBF, BUFSIZ);

    int opt = -1;
    const char *optstring = "A:PK:g:G:C:X";
    static struct option long_options[] = {
      {"auth",1,NULL,'A'},
      {"pwdp",1,NULL,'P'},
      {"pwdk",1,NULL,'K'},
      {"halg",1,NULL,'g'},
      {"kalg",1,NULL,'G'},
      {"context",1,NULL,'C'},
      {"passwdInHex",0,NULL,'X'},
      {0,0,0,0}
    };


    int returnVal = 0;
    int A_flag = 0,
        P_flag = 0,
        K_flag = 0,
        g_flag = 0,
        G_flag = 0,
        C_flag = 0;
    char *contextFile = NULL;

    while((opt = getopt_long(argc,argv,optstring,long_options,NULL)) != -1)
    {
        switch(opt)
        {
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
                returnVal = -1;
                break;
            }
            A_flag = 1;
            break;

        case 'P':
			fgets(pass, 40, stdin);
			index = strchr(pass, '\n');
			if (index)
				*index = '\0';
            sessionData.hmac.t.size = sizeof(sessionData.hmac.t) - 2;
            if(str2ByteStructure(pass,&sessionData.hmac.t.size,sessionData.hmac.t.buffer) != 0)
            {
                returnVal = -2;
                break;
            }

            P_flag = 1;
            break;
        case 'K':
            inSensitive.t.sensitive.userAuth.t.size = sizeof(inSensitive.t.sensitive.userAuth.t) - 2;
            if(str2ByteStructure(optarg,&inSensitive.t.sensitive.userAuth.t.size, inSensitive.t.sensitive.userAuth.t.buffer) != 0)
            {
                returnVal = -3;
                break;
            }
            K_flag = 1;
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
        case 'G':
            if(getSizeUint16Hex(optarg,&type) != 0)
            {
                showArgError(optarg, argv[0]);
                returnVal = -5;
                break;
            }
            printf("type = 0x%4.4x\n", type);
            G_flag = 1;
            break;
        case 'C':
            contextFile = optarg;
            if(contextFile == NULL || contextFile[0] == '\0')
            {
                returnVal = -8;
                break;
            }
            printf("contextFile = %s\n", contextFile);
            C_flag = 1;
            break;
        case 'X':
            hexPasswd = true;
            break;
        case ':':
//              printf("Argument %c needs a value!\n",optopt);
            returnVal = -9;
            break;
        case '?':
//              printf("Unknown Argument: %c\n",optopt);
            returnVal = -10;
            break;
        //default:
        //  break;
        }
        if(returnVal)
            break;
    };

    if(returnVal != 0)
        return returnVal;
    if(A_flag == 1 && g_flag == 1 && G_flag == 1)
    {
        returnVal = createPrimary(sapi_context, hierarchy, &inPublic, &inSensitive, type, nameAlg, P_flag, K_flag);

        if (returnVal == 0 && C_flag)
            returnVal = saveTpmContextToFile(sapi_context, handle2048rsa, contextFile);
        if(returnVal)
            return -12;
    }
    else
    {
        showArgMismatch(argv[0]);
        return -13;
    }

    return 0;
}
