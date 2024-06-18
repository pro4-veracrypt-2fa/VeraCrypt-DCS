#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/HttpLib.h>
#include <Library/PrintLib.h>
#include <Protocol/ServiceBinding.h>
#include <Library/CommonLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#define MAX_BUFFER_SIZE 0x100000

typedef struct {
    CHAR8* Key;
    CHAR8* Value;
} API_HEADER;

typedef struct {
    API_HEADER* Headers;
    UINTN HeaderCount;
} API_RESPONSE_HEADERS;

EFI_HANDLE __gImageHandle = NULL;
EFI_BOOT_SERVICES* __gBootServices = NULL;
BOOLEAN gRequestCallbackComplete = FALSE;
BOOLEAN gResponseCallbackComplete = FALSE;

VOID EFIAPI RequestCallback(IN EFI_EVENT Event, IN VOID *Context)
{
    gRequestCallbackComplete = TRUE;
}

VOID EFIAPI ResponseCallback(IN EFI_EVENT Event, IN VOID *Context)
{
    gResponseCallbackComplete = TRUE;
}

VOID VC2FAInit(EFI_HANDLE ImageHandle, EFI_BOOT_SERVICES* BootServices)
{
    __gImageHandle = ImageHandle;
    __gBootServices = BootServices;
}

EFI_STATUS CreateHTTPService(EFI_HTTP_PROTOCOL** Http)
{
    EFI_STATUS Status;
    EFI_SERVICE_BINDING_PROTOCOL* HttpSb = NULL;
    EFI_HANDLE HttpHandle = NULL;

    OUT_PRINT(L"Locating HTTP service binding protocol...\n");
    Status = gBS->LocateProtocol(&gEfiHttpServiceBindingProtocolGuid, NULL, (VOID**)&HttpSb);
    if (EFI_ERROR(Status)) {
        OUT_PRINT(L"Failed to locate HTTP service binding protocol: %r\n", Status);
        return Status;
    }

    OUT_PRINT(L"Creating HTTP service...\n");
    Status = HttpSb->CreateChild(HttpSb, &HttpHandle);
    if (EFI_ERROR(Status)) {
        OUT_PRINT(L"Failed to create HTTP service: %r\n", Status);
        return Status;
    }

    OUT_PRINT(L"Opening HTTP protocol...\n");
    Status = gBS->OpenProtocol(
        HttpHandle,
        &gEfiHttpProtocolGuid,
        (VOID**) Http,
        __gImageHandle,
        NULL,
        EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL
    );
    if (EFI_ERROR(Status)) {
        OUT_PRINT(L"Failed to open HTTP protocol: %r\n", Status);
        return Status;
    }

    return Status;
}

EFI_STATUS ConfigureHTTPService(EFI_HTTP_PROTOCOL* Http)
{
    EFI_HTTP_CONFIG_DATA HttpConfigData;
    EFI_HTTPv4_ACCESS_POINT IPv4Node;
    EFI_STATUS Status;

    ZeroMem(&HttpConfigData, sizeof(EFI_HTTP_CONFIG_DATA));
    ZeroMem(&IPv4Node, sizeof(EFI_HTTPv4_ACCESS_POINT));

    IPv4Node.UseDefaultAddress = TRUE;

    HttpConfigData.HttpVersion = HttpVersion11;
    HttpConfigData.TimeOutMillisec = 0xFFFFFFFF;
    HttpConfigData.LocalAddressIsIPv6 = FALSE;
    HttpConfigData.AccessPoint.IPv4Node = &IPv4Node;

    Status = Http->Configure(Http, &HttpConfigData);
    if (EFI_ERROR(Status)) {
        OUT_PRINT(L"Failed to configure HTTP service: %r\n", Status);
        return Status;
    }

    return Status;
}

EFI_STATUS HTTPPost(EFI_HTTP_PROTOCOL* Http, EFI_IPv4_ADDRESS* ServerIp, UINT16 Port, CHAR16* Path, API_HEADER* RequestHeaders, UINTN RequestHeaderCount, API_RESPONSE_HEADERS* ApiResponseHeaders)
{
    EFI_STATUS Status;
    EFI_HTTP_REQUEST_DATA RequestData;
    EFI_HTTP_MESSAGE RequestMessage, ResponseMessage;
    EFI_HTTP_TOKEN RequestToken, ResponseToken;
    CHAR16 Url[256];
    EFI_TIME Baseline, Current;
    UINTN Timer, Index;

    UnicodeSPrintAsciiFormat(
        Url, sizeof(Url),
        "http://%d.%d.%d.%d:%d%s",
        ServerIp->Addr[0], ServerIp->Addr[1],
        ServerIp->Addr[2], ServerIp->Addr[3], Port, Path
    );

    ZeroMem(&RequestData, sizeof(RequestData));
    RequestData.Method = HttpMethodPost;
    RequestData.Url = Url;

    ZeroMem(&RequestMessage, sizeof(RequestMessage));
    RequestMessage.Data.Request = &RequestData;
    RequestMessage.HeaderCount = RequestHeaderCount;
    RequestMessage.Headers = AllocateZeroPool(RequestHeaderCount * sizeof(EFI_HTTP_HEADER));

    for (Index = 0; Index < RequestHeaderCount; ++Index) {
        RequestMessage.Headers[Index].FieldName = RequestHeaders[Index].Key;
        RequestMessage.Headers[Index].FieldValue = RequestHeaders[Index].Value;
    }

    RequestMessage.BodyLength = 4;
    RequestMessage.Body = "leer";

    ZeroMem(&RequestToken, sizeof(EFI_HTTP_TOKEN));
    RequestToken.Status = EFI_SUCCESS;
    RequestToken.Message = &RequestMessage;

    OUT_PRINT(L"Sending POST request: %s\n", Url);
    Status = gBS->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_CALLBACK, RequestCallback, NULL, &RequestToken.Event);
    if (EFI_ERROR(Status)) {
        OUT_PRINT(L"Failed to create request event: %d\n", Status);
        return Status;
    }

    gRequestCallbackComplete = FALSE;
    Status = Http->Request(Http, &RequestToken);
    if (EFI_ERROR(Status)) {
        OUT_PRINT(L"Failed to send HTTP POST request: %d\n", Status);
        return Status;
    }

    Status = gRT->GetTime(&Baseline, NULL);
    if (EFI_ERROR(Status)) {
        OUT_PRINT(L"Failed to get baseline time: %d\n", Status);
        return Status;
    }

    for (Timer = 0; !gRequestCallbackComplete && Timer < 10000;) {
        Http->Poll(Http);
        if (!EFI_ERROR(gRT->GetTime(&Current, NULL)) && Current.Second != Baseline.Second) {
            Baseline = Current;
            ++Timer;
        }
    }

    if (!gRequestCallbackComplete) {
        Status = Http->Cancel(Http, &RequestToken);
        OUT_PRINT(L"Request callback not completed in time, canceling request: %d\n", Status);
        return Status;
    }

    ZeroMem(&ResponseMessage, sizeof(ResponseMessage));
    ResponseMessage.Data.Response = AllocateZeroPool(sizeof(EFI_HTTP_RESPONSE_DATA));
    if (ResponseMessage.Data.Response == NULL) {
        OUT_PRINT(L"Failed to allocate memory for response data\n");
        return EFI_OUT_OF_RESOURCES;
    }
    ResponseMessage.BodyLength = MAX_BUFFER_SIZE;
    ResponseMessage.Body = AllocateZeroPool(MAX_BUFFER_SIZE);

    ZeroMem(&ResponseToken, sizeof(EFI_HTTP_TOKEN));
    ResponseToken.Status = EFI_SUCCESS;
    ResponseToken.Message = &ResponseMessage;

    Status = gBS->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_CALLBACK, ResponseCallback, NULL, &ResponseToken.Event);
    if (EFI_ERROR(Status)) {
        OUT_PRINT(L"Failed to create response event: %r\n", Status);
        return Status;
    }

    gResponseCallbackComplete = FALSE;
    Status = Http->Response(Http, &ResponseToken);
    if (EFI_ERROR(Status)) {
        OUT_PRINT(L"Failed to receive HTTP response: %r\n", Status);
        return Status;
    }

    Status = gRT->GetTime(&Baseline, NULL);
    if (EFI_ERROR(Status)) {
        OUT_PRINT(L"Failed to get baseline time: %r\n", Status);
        return Status;
    }

    for (Timer = 0; !gResponseCallbackComplete && Timer < 10;) {
        Http->Poll(Http);
        if (!EFI_ERROR(gRT->GetTime(&Current, NULL)) && Current.Second != Baseline.Second) {
            Baseline = Current;
            ++Timer;
        }
    }

    if (!gResponseCallbackComplete) {
        Status = Http->Cancel(Http, &ResponseToken);
        OUT_PRINT(L"Response callback not completed in time, canceling response: %r\n", Status);
        return Status;
    }

    ApiResponseHeaders->HeaderCount = ResponseMessage.HeaderCount;
    ApiResponseHeaders->Headers = AllocateZeroPool(ResponseMessage.HeaderCount * sizeof(API_HEADER));
    for (Index = 0; Index < ResponseMessage.HeaderCount; ++Index) {
        ApiResponseHeaders->Headers[Index].Key = AllocateZeroPool(AsciiStrLen(ResponseMessage.Headers[Index].FieldName) + 1);
        AsciiStrCpyS(ApiResponseHeaders->Headers[Index].Key, AsciiStrLen(ResponseMessage.Headers[Index].FieldName) + 1, ResponseMessage.Headers[Index].FieldName);
        
        ApiResponseHeaders->Headers[Index].Value = AllocateZeroPool(AsciiStrLen(ResponseMessage.Headers[Index].FieldValue) + 1);
        AsciiStrCpyS(ApiResponseHeaders->Headers[Index].Value, AsciiStrLen(ResponseMessage.Headers[Index].FieldValue) + 1, ResponseMessage.Headers[Index].FieldValue);
    }

    OUT_PRINT(L"Response headers received:\n");
    for (Index = 0; Index < ApiResponseHeaders->HeaderCount; ++Index) {
        OUT_PRINT(L"%a: %a\n", ApiResponseHeaders->Headers[Index].Key, ApiResponseHeaders->Headers[Index].Value);
    }

    return EFI_SUCCESS;
}

VOID FreeAPIResponseHeaders(API_RESPONSE_HEADERS* ApiResponseHeaders)
{
    UINTN Index;
    if (ApiResponseHeaders != NULL) {
        for (Index = 0; Index < ApiResponseHeaders->HeaderCount; ++Index) {
            if (ApiResponseHeaders->Headers[Index].Key != NULL) {
                FreePool(ApiResponseHeaders->Headers[Index].Key);
            }
            if (ApiResponseHeaders->Headers[Index].Value != NULL) {
                FreePool(ApiResponseHeaders->Headers[Index].Value);
            }
        }
        if (ApiResponseHeaders->Headers != NULL) {
            FreePool(ApiResponseHeaders->Headers);
        }
    }
}

typedef struct {
    CHAR8 PcId[16];
    CHAR8 PcName[64];
    CHAR8 PairingCode[9];
} VC_AUTH_SETUP_INFO;

typedef struct {
    CHAR8 PcId[16];
} VC_AUTH_PUSH_INFO;

typedef struct {
    CHAR8 PcId[16];
    BOOLEAN Verified;
} VC_AUTH_AWAIT_INFO;

EFI_STATUS VCAuth2FASetup(EFI_HTTP_PROTOCOL* Http, EFI_IPv4_ADDRESS* ServerIp, VC_AUTH_SETUP_INFO* SetupInfo)
{
    EFI_STATUS Status;
    API_HEADER RequestHeaders[3];
    API_RESPONSE_HEADERS ApiResponseHeaders;
    CHAR8* PairingCode = NULL;

    // Setting up custom headers for the POST request
    RequestHeaders[0].Key = "Accept";
    RequestHeaders[0].Value = "application/json";
    RequestHeaders[1].Key = "Pc-Id";
    RequestHeaders[1].Value = SetupInfo->PcId;
    RequestHeaders[2].Key = "Pc-Name";
    RequestHeaders[2].Value = SetupInfo->PcName;

    OUT_PRINT(L"Performing HTTP POST request...\n");

    Status = HTTPPost(Http, ServerIp, 6000, L"/setup/new", RequestHeaders, 3, &ApiResponseHeaders);

    if (EFI_ERROR(Status)) {
        Print(L"Failed to perform HTTP POST request: %r\n", Status);
        return Status;
    }

    // Extracting the pairing code from response headers
    for (UINTN Index = 0; Index < ApiResponseHeaders.HeaderCount; ++Index) {
        if (AsciiStriCmp(ApiResponseHeaders.Headers[Index].Key, "Pairing-Code") == 0) {
            PairingCode = ApiResponseHeaders.Headers[Index].Value;
            break;
        }
    }

    if (PairingCode != NULL) {
        AsciiStrCpyS(SetupInfo->PairingCode, sizeof(SetupInfo->PairingCode), PairingCode);
        Print(L"Setup requested for PC '%a'; Enter the following code in your mobile app: %a\n", SetupInfo->PcName, SetupInfo->PairingCode);
    } else {
        Print(L"Failed to retrieve pairing code from the response headers.\n");
        return EFI_DEVICE_ERROR;
    }

    return EFI_SUCCESS;
}

EFI_STATUS VCAuth2FAPushRequest(EFI_HTTP_PROTOCOL* Http, EFI_IPv4_ADDRESS* ServerIp, VC_AUTH_PUSH_INFO* PushInfo)
{
    EFI_STATUS Status;
    API_HEADER RequestHeaders[2];
    API_RESPONSE_HEADERS ApiResponseHeaders;
    CHAR8* ComparisonCode = NULL;

    // Setting up custom headers for the POST request

    OUT_PRINT(L"Setting up custom headers for the POST request...\n");
    OUT_PRINT(L"Pc-Id: %a\n", PushInfo->PcId);
    RequestHeaders[0].Key = "Accept";
    RequestHeaders[0].Value = "application/json";
    RequestHeaders[1].Key = "Pc-Id";
    RequestHeaders[1].Value = PushInfo->PcId;

    OUT_PRINT(L"Performing HTTP POST request...\n");

    Status = HTTPPost(Http, ServerIp, 6000, L"/2fa/push", RequestHeaders, 2, &ApiResponseHeaders);

    if (EFI_ERROR(Status)) {
        Print(L"Failed to perform HTTP POST request: %r\n", Status);
        return Status;
    }

    // Extracting the comparison code from response headers
    for (UINTN Index = 0; Index < ApiResponseHeaders.HeaderCount; ++Index) {
        if (AsciiStriCmp(ApiResponseHeaders.Headers[Index].Key, "Comparison-Code") == 0) {
            ComparisonCode = ApiResponseHeaders.Headers[Index].Value;
            break;
        }
    }

    if (ComparisonCode != NULL) {
        Print(L"2FA request created. Enter the following code in your mobile app: %a\n", ComparisonCode);
    } else {
        Print(L"Failed to retrieve comparison code from the response headers.\n");
        return EFI_DEVICE_ERROR;
    }

    return EFI_SUCCESS;
}

EFI_STATUS VCAuth2FAAwaitRequest(EFI_HTTP_PROTOCOL* Http, EFI_IPv4_ADDRESS* ServerIp, VC_AUTH_AWAIT_INFO* AwaitInfo)
{
    EFI_STATUS Status;
    API_HEADER RequestHeaders[2];
    API_RESPONSE_HEADERS ApiResponseHeaders;
    CHAR8* VerifiedStr = NULL;

    // Setting up custom headers for the POST request
    RequestHeaders[0].Key = "Accept";
    RequestHeaders[0].Value = "application/json";
    RequestHeaders[1].Key = "Pc-Id";
    RequestHeaders[1].Value = AwaitInfo->PcId;

    OUT_PRINT(L"Performing HTTP POST request...\n");

    Status = HTTPPost(Http, ServerIp, 6000, L"/2fa/await", RequestHeaders, 2, &ApiResponseHeaders);

    if (EFI_ERROR(Status)) {
        Print(L"Failed to perform HTTP POST request: %r\n", Status);
        return Status;
    }

    // Extracting the verification status from response headers
    for (UINTN Index = 0; Index < ApiResponseHeaders.HeaderCount; ++Index) {
        if (AsciiStriCmp(ApiResponseHeaders.Headers[Index].Key, "Verified") == 0) {
            VerifiedStr = ApiResponseHeaders.Headers[Index].Value;
            break;
        }
    }

    if (VerifiedStr != NULL) {
        AwaitInfo->Verified = (AsciiStrCmp(VerifiedStr, "True") == 0);
    } else {
        Print(L"Failed to retrieve verification status from the response headers.\n");
        return EFI_DEVICE_ERROR;
    }

    return EFI_SUCCESS;
}

VOID 
__FlushInputDelay(
	IN UINTN delay
	) 
{
   EFI_INPUT_KEY  key;
   EFI_EVENT      InputEvents[2];
   UINTN          EventIndex = 0;

   InputEvents[0] = gST->ConIn->WaitForKey;
   gBS->CreateEvent(EVT_TIMER, 0, (EFI_EVENT_NOTIFY)NULL, NULL, &InputEvents[1]);
   gBS->SetTimer(InputEvents[1], TimerPeriodic, delay);
   while (EventIndex == 0) {
      gBS->WaitForEvent(2, InputEvents, &EventIndex);
      if (EventIndex == 0) {
         gST->ConIn->ReadKeyStroke(gST->ConIn, &key);
      }
   }
   gBS->CloseEvent(InputEvents[1]);
}

VOID
__FlushInput() {
	__FlushInputDelay(1000000);
}

EFI_INPUT_KEY
__KeyWait(
   CHAR16* Prompt,
   UINTN mDelay,
   UINT16 scanCode,
   UINT16 unicodeChar)
{
   EFI_INPUT_KEY  key;
   EFI_EVENT      InputEvents[2];
   UINTN          EventIndex;

   __FlushInput();
   key.ScanCode = scanCode;
   key.UnicodeChar = unicodeChar;

   InputEvents[0] = gST->ConIn->WaitForKey;

   gBS->CreateEvent(EVT_TIMER, 0, (EFI_EVENT_NOTIFY)NULL, NULL, &InputEvents[1]);
   gBS->SetTimer(InputEvents[1], TimerPeriodic, 10000000);
   while (mDelay > 0) {
      OUT_PRINT(Prompt, mDelay);
      gBS->WaitForEvent(2, InputEvents, &EventIndex);
      if (EventIndex == 0) {
			if (EFI_ERROR(gST->ConIn->ReadKeyStroke(gST->ConIn, &key))) {
				continue;
			}
         break;
      }
      else {
         mDelay--;
      }
   }
   OUT_PRINT(Prompt, mDelay);
   gBS->CloseEvent(InputEvents[1]);
   return key;
}

VOID VCAuth2FASampleRequest()
{
    EFI_STATUS Status;
    EFI_HTTP_PROTOCOL* Http = NULL;
    EFI_IPv4_ADDRESS ServerIp;

    ServerIp.Addr[0] = 152;
    ServerIp.Addr[1] = 53;
    ServerIp.Addr[2] = 3;
    ServerIp.Addr[3] = 7;

    OUT_PRINT(L"Creating HTTP service...\n");
    Status = CreateHTTPService(&Http);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to create HTTP service: %r\n", Status);
        return;
    }

    OUT_PRINT(L"Configuring HTTP service...\n");
    Status = ConfigureHTTPService(Http);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to configure HTTP service: %r\n", Status);
        return;
    }

    VC_AUTH_SETUP_INFO SetupInfo = { 0 };
    AsciiStrCpyS(SetupInfo.PcId, sizeof(SetupInfo.PcId), "test");
    AsciiStrCpyS(SetupInfo.PcName, sizeof(SetupInfo.PcName), "Test PC");
    Status = VCAuth2FASetup(Http, &ServerIp, &SetupInfo);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to perform setup: %r\n", Status);
        return;
    }

    // Prompt the user to press ENTER to continue using KeyWait
    __KeyWait(L"Press ENTER to after you have entered the code in your mobile app ...\n", 30000, 0, CHAR_CARRIAGE_RETURN);

    OUT_PRINT(L"Received from VCAuth2FASetup: %a\n", SetupInfo.PcId);

    VC_AUTH_PUSH_INFO PushInfo = { 0 };
    // CopyMem(PushInfo.PcId, SetupInfo.PcId, sizeof(PushInfo.PcId));
    AsciiStrCpyS(PushInfo.PcId, sizeof(PushInfo.PcId), "test");

    OUT_PRINT(L"In PUSH_INFO: %a\n", SetupInfo.PcId);

    Status = VCAuth2FAPushRequest(Http, &ServerIp, &PushInfo);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to perform 2FA push request: %r\n", Status);
        return;
    }

    Print(L"2FA push request sent. Please approve the request in your mobile app.\n");

    VC_AUTH_AWAIT_INFO AwaitInfo = { 0 };
    // AsciiStrCpyS(AwaitInfo.PcId, sizeof(AwaitInfo.PcId), PushInfo.PcId);
    AsciiStrCpyS(AwaitInfo.PcId, sizeof(AwaitInfo.PcId), "test");
    Status = VCAuth2FAAwaitRequest(Http, &ServerIp, &AwaitInfo);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to perform 2FA await request: %r\n", Status);
        return;
    }

    if (AwaitInfo.Verified) {
        Print(L"2FA request was verified successfully.\n");
    } else {
        Print(L"2FA request was not verified.\n");
    }
}
