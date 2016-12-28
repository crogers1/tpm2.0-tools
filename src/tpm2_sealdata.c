
#include <stdarg.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <getopt.h>
#include <stdbool.h>

#include <sapi/tpm20.h>
#include "sample.h"
#include <tcti/tcti_socket.h>
#include "common.h"

#define SET_PCR_SELECT_BIT( pcrSelection, pcr ) \
	(pcrSelection).pcrSelect[( (pcr)/8 )] |= ( 1 << ( (pcr) % 8) );

TPMS_AUTH_COMMAND sessionData;
int debugLevel = 0;
TPM_HANDLE handle2048rsa;
bool hexPasswd = false;

int setAlgCreate(TPMI_ALG_PUBLIC type,TPMI_ALG_HASH nameAlg,TPM2B_PUBLIC *inPublic, int I_flag)
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
    inPublic->t.publicArea.objectAttributes.restricted = 0;
    inPublic->t.publicArea.objectAttributes.userWithAuth = 1;
    inPublic->t.publicArea.objectAttributes.decrypt = 1;
    inPublic->t.publicArea.objectAttributes.sign = 1;
    inPublic->t.publicArea.objectAttributes.fixedTPM = 1;
    inPublic->t.publicArea.objectAttributes.fixedParent = 1;
    inPublic->t.publicArea.objectAttributes.sensitiveDataOrigin = 1;

    inPublic->t.publicArea.type = type;
    switch(type)
    {
    case TPM_ALG_RSA:
        inPublic->t.publicArea.parameters.rsaDetail.symmetric.algorithm = TPM_ALG_NULL;
        inPublic->t.publicArea.parameters.rsaDetail.scheme.scheme = TPM_ALG_NULL;
        inPublic->t.publicArea.parameters.rsaDetail.keyBits = 2048;
        inPublic->t.publicArea.parameters.rsaDetail.exponent = 0;
        inPublic->t.publicArea.unique.rsa.t.size = 0;
        break;

    case TPM_ALG_KEYEDHASH:
        inPublic->t.publicArea.unique.keyedHash.t.size = 0;
        inPublic->t.publicArea.objectAttributes.decrypt = 0;
        if (I_flag)
        {
            // sealing
            inPublic->t.publicArea.objectAttributes.sign = 0;
            inPublic->t.publicArea.objectAttributes.sensitiveDataOrigin = 0;
            inPublic->t.publicArea.parameters.keyedHashDetail.scheme.scheme = TPM_ALG_NULL;
        }
        else
        {
            // hmac
            inPublic->t.publicArea.objectAttributes.sign = 1;
            inPublic->t.publicArea.parameters.keyedHashDetail.scheme.scheme = TPM_ALG_HMAC;
            inPublic->t.publicArea.parameters.keyedHashDetail.scheme.details.hmac.hashAlg = nameAlg;  //for tpm2_hmac multi alg
        }
        break;

    case TPM_ALG_ECC:
        inPublic->t.publicArea.parameters.eccDetail.symmetric.algorithm = TPM_ALG_NULL;
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

TPM_RC Create(TPMI_DH_OBJECT parentHandle, TPM2B_PUBLIC *inPublic, TPM2B_SENSITIVE_CREATE *inSensitive, TPMI_ALG_PUBLIC type, TPMI_ALG_HASH nameAlg, 
				const char *outputPublicFilepath, const char *outputPrivateFilepath, int o_flag, int O_flag, int I_flag, int b_flag, UINT32 objectAttributes, 
				SESSION *policySession)
{

    TPM_RC rval;
    TPMS_AUTH_RESPONSE sessionDataOut;
    TSS2_SYS_CMD_AUTHS sessionsData;
    TSS2_SYS_RSP_AUTHS sessionsDataOut;
    TPMS_AUTH_COMMAND *sessionDataArray[1];
    TPMS_AUTH_RESPONSE *sessionDataOutArray[1];

    TPM2B_DATA              outsideInfo = { { 0, } };
    TPML_PCR_SELECTION      creationPCR;
    TPM2B_PUBLIC            outPublic = { { 0, } };
    TPM2B_PRIVATE           outPrivate = { { sizeof(TPM2B_PRIVATE)-2, } };

    TPM2B_CREATION_DATA     creationData = { { 0, } };
    TPM2B_DIGEST            creationHash = { { sizeof(TPM2B_DIGEST)-2, } };
    TPMT_TK_CREATION        creationTicket = { 0, };

    sessionDataArray[0] = &sessionData;
    sessionDataOutArray[0] = &sessionDataOut;

    sessionsDataOut.rspAuths = &sessionDataOutArray[0];
    sessionsData.cmdAuths = &sessionDataArray[0];

    sessionsDataOut.rspAuthsCount = 1;

    sessionData.sessionHandle = TPM_RS_PW;
    sessionData.nonce.t.size = 0;

    sessionsData.cmdAuthsCount = 1;
    sessionsData.cmdAuths[0] = &sessionData;

	//Clear hmac password field in sessionData, we're sealing using adminWithPolicy
	sessionData.hmac.t.size = 0;

    if (sessionData.hmac.t.size > 0 && hexPasswd)
    {
        sessionData.hmac.t.size = sizeof(sessionData.hmac) - 2;
        if (hex2ByteStructure((char *)sessionData.hmac.t.buffer,
                              &sessionData.hmac.t.size,
                              sessionData.hmac.t.buffer) != 0)
        {
            printf( "Failed to convert Hex format password for parent Passwd.\n");
            return -1;
        }
    }

    if (inSensitive->t.sensitive.userAuth.t.size > 0 && hexPasswd)
    {
        inSensitive->t.sensitive.userAuth.t.size = sizeof(inSensitive->t.sensitive.userAuth) - 2;
        if (hex2ByteStructure((char *)inSensitive->t.sensitive.userAuth.t.buffer,
                              &inSensitive->t.sensitive.userAuth.t.size,
                              inSensitive->t.sensitive.userAuth.t.buffer) != 0)
        {
            printf( "Failed to convert Hex format password for object Passwd.\n");
            return -1;
        }
    }
    inSensitive->t.size = inSensitive->t.sensitive.userAuth.b.size + 2;

    if(setAlgCreate(type, nameAlg, inPublic, I_flag))
        return -1;

    if(b_flag == 1)
        inPublic->t.publicArea.objectAttributes.val = objectAttributes;

	creationPCR.count = 0;

    rval = Tss2_Sys_Create(sysContext, parentHandle, &sessionsData, inSensitive, inPublic,
            &outsideInfo, &creationPCR, &outPrivate,&outPublic,&creationData, &creationHash,
            &creationTicket, &sessionsDataOut);

    if(rval != TPM_RC_SUCCESS)
    {
        printf("\nCreate Object Failed ! ErrorCode: 0x%0x\n\n",rval);
        return -2;
    }
    printf("\nCreate Object Succeed !\n");

    if(o_flag == 1)
    {
        if(saveDataToFile(outputPublicFilepath, (UINT8 *)&outPublic, sizeof(outPublic)))
            return -3;
    }
    if(O_flag == 1)
    {
        if(saveDataToFile(outputPrivateFilepath, (UINT8 *)&outPrivate, sizeof(outPrivate)))
            return -4;
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

	/*	
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
	}*/
	
	//remove this line if uncommenting above block
	inSensitive.t.sensitive.userAuth.t.size = 0;

	inSensitive.t.sensitive.data.t.size = 0;
	inSensitive.t.size = inSensitive.t.sensitive.userAuth.b.size + 2;

    if(setAlg(type, nameAlg, inPublic))
        return -1;

    creationPCR.count = 0;

    rval = Tss2_Sys_CreatePrimary(sysContext, hierarchy, &sessionsData, &inSensitive, inPublic, &outsideInfo, &creationPCR, &handle2048rsa, &outPublic, &creationData, &creationHash, &creationTicket, &name, &sessionsDataOut);
    if(rval != TPM_RC_SUCCESS)
    {
        printf("\nCreatePrimary Failed ! ErrorCode: 0x%0x\n\n",rval);
        return -2;
    }
    printf("\nCreatePrimary Succeed ! Handle: 0x%8.8x\n\n",handle2048rsa);

    return 0;

}

//TODO: Might need to include inSensitive for CreatePrimary...maybe
int seal(TPM2B_SENSITIVE_CREATE *inSensitive, TPMI_ALG_PUBLIC type, TPMI_ALG_HASH nameAlg, char *outputPublicFilepath, char *outputPrivateFilepath,
			int o_flag, int O_flag, int I_flag, int b_flag, int P_flag, UINT32 objectAttributes, UINT32 pcr, TPMI_RH_HIERARCHY hierarchy)
{

	//Create trial policy if pcr specified
	//Createprimary for parent context
	//tpm2_create to seal data
	UINT32 rval;
	SESSION *policySession;
	TPM2B_PUBLIC inPublic;
	TPM2B_DIGEST policyDigest;

	//Build a trial policy gated by the provided PCR
	rval = BuildPolicyExternal(sysContext, &policySession, true, pcr, &policyDigest, nameAlg);
	if(rval != TPM_RC_SUCCESS)
	{
		printf("BuildPolicy failed, errorcode: 0x%x\n", rval);
		return rval;
	}
	
	//Create the parent context
	rval = CreatePrimary(hierarchy, &inPublic, TPM_ALG_RSA, nameAlg, P_flag); 
	if(rval != TPM_RC_SUCCESS)
	{
		printf("CreatePrimary failed, errorcode: 0x%x\n", rval);
		return rval;
	}
	
	inPublic.t.publicArea.authPolicy.t.size = policyDigest.t.size;
	memcpy(inPublic.t.publicArea.authPolicy.t.buffer, policyDigest.t.buffer, policyDigest.t.size);
	//Seal the provided data
	rval = Create(handle2048rsa, &inPublic, inSensitive, type, nameAlg, outputPublicFilepath, outputPrivateFilepath, o_flag, O_flag, I_flag, b_flag, objectAttributes, policySession);
	if(rval != TPM_RC_SUCCESS)
	{
		printf("Create failed, errorcode: 0x%x\n", rval);
		return rval;
	}

}


void showHelp(const char *name)
{
    printf("\n%s  [options]\n"
        "\n"
        "-h, --help             Display command tool usage info;\n"
        "-v, --version          Display command tool version info\n"
        "-K, --pwdk   <string>  password for key, optional\n"
        "-A, --auth <o | p |...>  the authorization used to authorize thecommands\n"
            "\to  TPM_RH_OWNER\n"
            "\tp  TPM_RH_PLATFORM\n"
            "\te  TPM_RH_ENDORSEMENT\n"
            "\tn  TPM_RH_NULL\n"
        "-P, --pwdp <string>      password for hierarchy, optional\n"
        "-g, --halg   <hexAlg>  algorithm used for computing the Name of the object\n"
            "\t0x0004  TPM_ALG_SHA1\n"
            "\t0x000B  TPM_ALG_SHA256\n"
            "\t0x000C  TPM_ALG_SHA384\n"
            "\t0x000D  TPM_ALG_SHA512\n"
            "\t0x0012  TPM_ALG_SM3_256\n"
        "-G, --kalg   <hexAlg>  algorithm associated with this object\n"
            "\t0x0001  TPM_ALG_RSA\n"
            "\t0x0008  TPM_ALG_KEYEDHASH\n"
            "\t0x0023  TPM_ALG_ECC\n"
            "\t0x0025  TPM_ALG_SYMCIPHER\n"
        "-b, --objectAttribute <hexAttributeDWord>  object attributes, optional\n"
        "-r, --pcr <pcrID> pcr to base access policy on, required\n"
        "-I, --inFile          <dataFileToBeSealed>  data file to be sealed\n"
        "-o, --opu <publicKeyFileName>  the output file which contains the public key, optional\n"
        "-O, --opr <privateKeyFileName> the output file which contains the private key, optional\n"
        "-n, --contextFile <filepath> the output file which contains the parent context, optional\n"
        "-X, --passwdInHex              passwords given by any options are hex format.\n"

        "-p, --port  <port number>  The Port number, default is %d, optional\n"
        "-d, --debugLevel <0|1|2|3> The level of debug message, default is 0, optional\n"
            "\t0 (high level test results)\n"
            "\t1 (test app send/receive byte streams)\n"
            "\t2 (resource manager send/receive byte streams)\n"
            "\t3 (resource manager tables)\n"
        "\n"
        "Example:\n"
        "%s \n"
        ,name, DEFAULT_RESMGR_TPM_PORT, name, name, name);
}

int main(int argc, char* argv[])
{

    char hostName[200] = DEFAULT_HOSTNAME;
    int port = DEFAULT_RESMGR_TPM_PORT;

    TPM2B_SENSITIVE_CREATE  inSensitive;
    inSensitive.t.sensitive.data.t.size = 0;
    TPMI_ALG_PUBLIC type;
    TPMI_ALG_HASH nameAlg;
    UINT32 objectAttributes = 0;
    char opuFilePath[PATH_MAX] = {0};
    char oprFilePath[PATH_MAX] = {0};
    char contextFilePath[PATH_MAX] = {0};
    TPMI_RH_HIERARCHY hierarchy = TPM_RH_NULL;

	UINT32 pcr = -1;

    setbuf(stdout, NULL);
    setvbuf (stdout, NULL, _IONBF, BUFSIZ);

    int opt = -1;
    const char *optstring = "hvH:P:K:g:G:A:I:L:o:O:p:d:c:b:r:n:X";
    static struct option long_options[] = {
      {"help",0,NULL,'h'},
      {"version",0,NULL,'v'},
      {"pwdp",1,NULL,'P'},
      {"pwdk",1,NULL,'K'},
      {"halg",1,NULL,'g'},
      {"kalg",1,NULL,'G'},
      {"objectAttributes",1,NULL,'b'},
      {"auth",1,NULL,'A'},
      {"pcr",1,NULL,'r'},
      {"inFile",1,NULL,'I'},
      {"opu",1,NULL,'o'},
      {"opr",1,NULL,'O'},
      {"contextFile",1,NULL,'n'},
      {"passwdInHex",0,NULL,'X'},
      {"port",1,NULL,'p'},
      {"debugLevel",1,NULL,'d'},
      {0,0,0,0}
    };

    int returnVal = 0;
    int flagCnt = 0;
    int h_flag = 0,
        v_flag = 0,
        P_flag = 0,
        K_flag = 0,
        g_flag = 0,
        G_flag = 0,
        A_flag = 0,
        I_flag = 0,
        o_flag = 0,
        c_flag = 0,
        b_flag = 0,
        r_flag = 0,
        n_flag = 0,
        O_flag = 0;

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
        case 'P':
            sessionData.hmac.t.size = sizeof(sessionData.hmac.t) - 2;
            if(str2ByteStructure(optarg,&sessionData.hmac.t.size,sessionData.hmac.t.buffer) != 0)
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
        case 'b':
            if(getSizeUint32Hex(optarg,&objectAttributes) != 0)
            {
                showArgError(optarg, argv[0]);
                returnVal = -6;
                break;
            }
            b_flag = 1;
            break;
        case 'I':
            inSensitive.t.sensitive.data.t.size = sizeof(inSensitive.t.sensitive.data) - 2;
            if(loadDataFromFile(optarg, inSensitive.t.sensitive.data.t.buffer, &inSensitive.t.sensitive.data.t.size) != 0)
            {
                returnVal = -7;
                break;
            }
            I_flag = 1;
            printf("inSensitive.t.sensitive.data.t.size = %d\n",inSensitive.t.sensitive.data.t.size);
            break;
        case 'o':
            safeStrNCpy(opuFilePath, optarg, sizeof(opuFilePath));
            if(checkOutFile(opuFilePath) != 0)
            {
                returnVal = -9;
                break;
            }
            o_flag = 1;
            break;
        case 'O':
            safeStrNCpy(oprFilePath, optarg, sizeof(oprFilePath));
            if(checkOutFile(oprFilePath) != 0)
            {
                returnVal = -10;
                break;
            }
            O_flag = 1;
            break;
        case 'n':
            safeStrNCpy(contextFilePath, optarg, sizeof(contextFilePath));
            if(checkOutFile(contextFilePath) != 0)
            {
                returnVal = -21;
                break;
            }
            n_flag = 1;
            break;
        case 'r':
			if ( getPcrId(optarg, &pcr) )
			{
				printf("Invalid pcr value.\n");
				returnVal = -11;
			}
			r_flag = 1;
			break;
        case 'X':
            hexPasswd = true;
            break;
        case 'p':
            if( getPort(optarg, &port) )
            {
                printf("Incorrect port number.\n");
                returnVal = -12;
            }
            break;
        case 'd':
            if( getDebugLevel(optarg, &debugLevel) )
            {
                printf("Incorrect debug level.\n");
                returnVal = -13;
            }
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
        case ':':
            returnVal = -14;
            break;
        case '?':
            returnVal = -15;
            break;
        }
        if(returnVal)
            break;
    };

    if(returnVal != 0)
        return returnVal;

    if(P_flag == 0)
        sessionData.hmac.t.size = 0;
    if(K_flag == 0)
        inSensitive.t.sensitive.userAuth.t.size = 0;

    *((UINT8 *)((void *)&sessionData.sessionAttributes)) = 0;

    flagCnt = h_flag + v_flag + g_flag + G_flag + I_flag + A_flag + r_flag;
    if(flagCnt == 1)
    {
        if(h_flag == 1)
            showHelp(argv[0]);
        else if(v_flag == 1)
            showVersion(argv[0]);
        else
        {
            showArgMismatch(argv[0]);
            return -16;
        }
    }
    else if(flagCnt >= 5 && I_flag == 1 && g_flag == 1 && G_flag == 1 && A_flag == 1 && r_flag == 1)
    {
        prepareTest(hostName, port, debugLevel);

        if(returnVal == 0)
            returnVal = seal(&inSensitive, type, nameAlg, opuFilePath, oprFilePath, o_flag, O_flag, I_flag, b_flag, P_flag, objectAttributes, pcr, hierarchy);

        if (returnVal == 0 && n_flag)
            returnVal = saveTpmContextToFile(sysContext, handle2048rsa, contextFilePath);

        finishTest();

        if(returnVal)
            return -17;
    }
    else
    {
        showArgMismatch(argv[0]);
        return -18;
    }
    return 0;
}
