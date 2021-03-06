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
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <getopt.h>

#include <sapi/tpm20.h>
#include <tcti/tcti_socket.h>
#include "common.h"

int debugLevel = 0;

TPMS_AUTH_COMMAND sessionData;
bool hexPasswd = false;

int rsaDecrypt(TPMI_DH_OBJECT keyHandle, TPM2B_PUBLIC_KEY_RSA *cipherText, const char *outFilePath)
{
    UINT32 rval;
    TPMT_RSA_DECRYPT inScheme;
    TPM2B_DATA label;
    TPM2B_PUBLIC_KEY_RSA message = { { sizeof(TPM2B_PUBLIC_KEY_RSA)-2, } };

    TSS2_SYS_CMD_AUTHS sessionsData;
    TPMS_AUTH_RESPONSE sessionDataOut;
    TSS2_SYS_RSP_AUTHS sessionsDataOut;
    TPMS_AUTH_COMMAND *sessionDataArray[1];
    TPMS_AUTH_RESPONSE *sessionDataOutArray[1];

    sessionDataArray[0] = &sessionData;
    sessionsData.cmdAuths = &sessionDataArray[0];
    sessionDataOutArray[0] = &sessionDataOut;
    sessionsDataOut.rspAuths = &sessionDataOutArray[0];
    sessionsDataOut.rspAuthsCount = 1;
    sessionsData.cmdAuthsCount = 1;

    sessionData.sessionHandle = TPM_RS_PW;
    sessionData.nonce.t.size = 0;
    *((UINT8 *)((void *)&sessionData.sessionAttributes)) = 0;
    if (sessionData.hmac.t.size > 0 && hexPasswd)
    {
        sessionData.hmac.t.size = sizeof(sessionData.hmac) - 2;
        if (hex2ByteStructure((char *)sessionData.hmac.t.buffer,
                              &sessionData.hmac.t.size,
                              sessionData.hmac.t.buffer) != 0)
        {
            printf( "Failed to convert Hex format password for key Passwd.\n");
            return -1;
        }
    }

    inScheme.scheme = TPM_ALG_RSAES;
    label.t.size = 0;

    rval = Tss2_Sys_RSA_Decrypt(sysContext, keyHandle, &sessionsData, cipherText, &inScheme, &label, &message, &sessionsDataOut);
    if(rval != TPM_RC_SUCCESS)
    {
        printf("rsaDecrypt failed, error code: 0x%x\n", rval);
        return -1;
    }
    printf("\nRSA Decrypt succ.\n");

    if(saveDataToFile(outFilePath, message.t.buffer, message.t.size))
    {
        printf("OutFile %s failed!\n", outFilePath);
        return -1;
    }
    printf("OutFile %s completed!\n", outFilePath);

    return 0;
}

void showHelp(const char *name)
{
    printf("\n%s  [options]\n"
        "\n"
        "-h, --help               Display command tool usage info;\n"
        "-v, --version            Display command tool version info\n"
        "-k, --keyHandle<hexHandle>  the public portion of RSA key to use for decryption  \n"
        "-c, --keyContext <filename> filename of the key context used for the operation\n"
        "-P, --pwdk     <password>   the password of key, optional\n"
        "-I, --inFile   <filePath>   Input file path, containing the data to be decrypted\n"
        "-o, --outFile  <filePath>   Output file path, record the decrypted data\n"
        "-X, --passwdInHex           passwords given by any options are hex format.\n"
        "-p, --port   <port number>  The Port number, default is %d, optional\n"
        "-d, --debugLevel <0|1|2|3>  The level of debug message, default is 0, optional\n"
        "\t0 (high level test results)\n"
        "\t1 (test app send/receive byte streams)\n"
        "\t2 (resource manager send/receive byte streams)\n"
        "\t3 (resource manager tables)\n"
    "\n"
        "Example:\n"
        "%s -k 0x81010001 -I <filePath> -o <filePath>\n"
        "%s -k 0x81010001 -P 123abc -X -I <filePath> -o <filePath>\n"
        ,name, DEFAULT_RESMGR_TPM_PORT, name, name);
}

int main(int argc, char* argv[])
{
    char hostName[200] = DEFAULT_HOSTNAME;
    int port = DEFAULT_RESMGR_TPM_PORT;

    TPMI_DH_OBJECT keyHandle;
    TPM2B_PUBLIC_KEY_RSA cipherText;
    char outFilePath[PATH_MAX] = {0};
    char *contextKeyFile = NULL;

    setbuf(stdout, NULL);
    setvbuf (stdout, NULL, _IONBF, BUFSIZ);

    int opt = -1;
    const char *optstring = "hvk:P:I:o:p:d:c:X";
    static struct option long_options[] = {
      {"help",0,NULL,'h'},
      {"version",0,NULL,'v'},
      {"keyHandle",1,NULL,'k'},
      {"pwdk",1,NULL,'P'},
      {"inFile",1,NULL,'I'},
      {"outFile",1,NULL,'o'},
      {"port",1,NULL,'p'},
      {"debugLevel",1,NULL,'d'},
      {"keyContext",1,NULL,'c'},
      {"passwdInHex",0,NULL,'X'},
      {0,0,0,0}
    };

    int returnVal = 0;
    int flagCnt = 0;
    int h_flag = 0,
        v_flag = 0,
        k_flag = 0,
        P_flag = 0,
        I_flag = 0,
        c_flag = 0,
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
        case 'k':
            if(getSizeUint32Hex(optarg,&keyHandle) != 0)
            {
                returnVal = -1;
                break;
            }
            k_flag = 1;
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
        case 'I':
            cipherText.t.size = sizeof(cipherText) - 2;
            if(loadDataFromFile(optarg, cipherText.t.buffer, &cipherText.t.size) != 0)
            {
                returnVal = -3;
                break;
            }
            I_flag = 1;
            break;
        case 'o':
            safeStrNCpy(outFilePath, optarg, sizeof(outFilePath));
            if(checkOutFile(outFilePath) != 0)
            {
                returnVal = -4;
                break;
            }
            o_flag = 1;
            break;
        case 'p':
            if( getPort(optarg, &port) )
            {
                printf("Incorrect port number.\n");
                returnVal = -5;
            }
            break;
        case 'd':
            if( getDebugLevel(optarg, &debugLevel) )
            {
                printf("Incorrect debug level.\n");
                returnVal = -6;
            }
            break;
        case 'c':
            contextKeyFile = optarg;
            if(contextKeyFile == NULL || contextKeyFile[0] == '\0')
            {
                returnVal = -7;
                break;
            }
            printf("contextKeyFile = %s\n", contextKeyFile);
            c_flag = 1;
            break;
        case 'X':
            hexPasswd = true;
            break;
        case ':':
//              printf("Argument %c needs a value!\n",optopt);
            returnVal = -8;
            break;
        case '?':
//              printf("Unknown Argument: %c\n",optopt);
            returnVal = -9;
            break;
        //default:
        //  break;
        }
        if(returnVal)
            break;
    };

    if(returnVal != 0)
        return returnVal;

    if(P_flag == 0)
        sessionData.hmac.t.size = 0;

    flagCnt = h_flag + v_flag + k_flag + I_flag + o_flag + c_flag;

    if(flagCnt == 1)
    {
        if(h_flag == 1)
            showHelp(argv[0]);
        else if(v_flag == 1)
            showVersion(argv[0]);
        else
        {
            showArgMismatch(argv[0]);
            return -10;
        }
    }
    else if((flagCnt == 3) && (k_flag == 1 || c_flag == 1) && (I_flag == 1) && (o_flag == 1))
    {
        prepareTest(hostName, port, debugLevel);

        if(c_flag)
            returnVal = loadTpmContextFromFile(sysContext, &keyHandle, contextKeyFile);
        if(returnVal == 0)
            returnVal = rsaDecrypt(keyHandle, &cipherText, outFilePath);

        finishTest();

        if(returnVal)
            return -11;
    }
    else
    {
        showArgMismatch(argv[0]);
        return -12;
    }

    return 0;
}
