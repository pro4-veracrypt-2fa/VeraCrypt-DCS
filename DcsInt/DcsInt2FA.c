#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/HttpLib.h>
#include <Library/PrintLib.h>
#include <Protocol/ServiceBinding.h>
#include <Library/CommonLib.h>

EFI_HANDLE __gImageHandle = NULL;
EFI_BOOT_SERVICES* __gBootServices = NULL;

VOID VC2FAInit(EFI_HANDLE ImageHandle, EFI_BOOT_SERVICES* BootServices)
{
    __gImageHandle = ImageHandle;
    __gBootServices = BootServices;
}

EFI_STATUS CreateHTTPService(EFI_HTTP_PROTOCOL** Http)
{
    EFI_STATUS Status = EFI_ABORTED;
    EFI_SERVICE_BINDING_PROTOCOL* HttpSb = NULL;
    EFI_HANDLE HttpHandle = NULL;

    // Locate the HTTP Service Binding Protocol
    OUT_PRINT(L"boot service table at %p\n", gBS);
    OUT_PRINT(L"Locating HTTP service binding protocol...\n");
    Status = gBS->LocateProtocol(&gEfiHttpServiceBindingProtocolGuid, NULL, (VOID**)&HttpSb);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    // Create a child handle and get the HTTP protocol interface
    OUT_PRINT(L"Creating HTTP service...\n");
    Status = HttpSb->CreateChild(HttpSb, &HttpHandle);
    if (EFI_ERROR(Status)) {
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

    return Status;
}

EFI_STATUS ConfigureHTTPService(EFI_HTTP_PROTOCOL* Http)
{
    EFI_HTTP_CONFIG_DATA HttpConfigData;
    EFI_HTTPv4_ACCESS_POINT IPv4Node;
    EFI_STATUS Status;

    ZeroMem(&HttpConfigData, sizeof(EFI_HTTP_CONFIG_DATA));
    ZeroMem(&IPv4Node, sizeof(EFI_HTTPv4_ACCESS_POINT));

    // Set up default local address settings for IPv4
    IPv4Node.UseDefaultAddress = TRUE; // Assuming DHCP or default configuration

    // Configure the HTTP data structure
    HttpConfigData.HttpVersion = HttpVersion11;
    HttpConfigData.TimeOutMillisec = 0xFFFFFFFF; // Infinite timeout
    HttpConfigData.LocalAddressIsIPv6 = FALSE;
    HttpConfigData.AccessPoint.IPv4Node = &IPv4Node;

    // Apply the configuration to the HTTP protocol instance
    Status = Http->Configure(Http, &HttpConfigData);
    return Status;
}

VOID EFIAPI RequestEvent(IN EFI_EVENT Event, IN VOID *Context)
{
    OUT_PRINT(L"Request event\n");
}

EFI_STATUS AccessServerResource(EFI_HTTP_PROTOCOL* Http, EFI_IPv4_ADDRESS* ServerIp, UINT16 Port) {
    EFI_STATUS Status;
    EFI_HTTP_REQUEST_DATA RequestData;
    EFI_HTTP_MESSAGE RequestMessage, ResponseMessage;
    EFI_HTTP_TOKEN RequestToken, ResponseToken;
    EFI_EVENT ResponseEvent;
    CHAR16 Url[256];
    EFI_HTTP_METHOD RequestMethod = HttpMethodGet;
    EFI_HTTP_HEADER RequestHeaders[] = {
        { "Accept", "application/json" }
    };
    UINTN ResponseBufferSize = 1024;
    CHAR8 ResponseBuffer[1024];

    ZeroMem(ResponseBuffer, sizeof(ResponseBuffer));

    // Construct the URL
    UnicodeSPrintAsciiFormat(
        Url, sizeof(Url),
        "http://%d.%d.%d.%d:%d/",
        ServerIp->Addr[0], ServerIp->Addr[1],
        ServerIp->Addr[2], ServerIp->Addr[3], Port
    );

    // Prepare the request
    ZeroMem(&RequestData, sizeof(RequestData));
    RequestData.Method = RequestMethod;
    RequestData.Url = Url;

    ZeroMem(&RequestMessage, sizeof(RequestMessage));
    RequestMessage.Data.Request = &RequestData;
    RequestMessage.HeaderCount = sizeof(RequestHeaders) / sizeof(EFI_HTTP_HEADER);
    RequestMessage.Headers = RequestHeaders;
    RequestMessage.BodyLength = 0; // GET request has no body
    RequestMessage.Body = NULL;

    // Prepare the token for the request
    ZeroMem(&ResponseEvent, sizeof(EFI_EVENT));

    OUT_PRINT(L"Request: %s\n", Url);
    ZeroMem(&RequestToken, sizeof(EFI_HTTP_TOKEN));
    RequestToken.Event = NULL;
    RequestToken.Message = &RequestMessage;

    // Send the request
    OUT_PRINT(L"Sending request...\n");
    Status = Http->Request(Http, &RequestToken);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    // Prepare the response
    ZeroMem(&ResponseMessage, sizeof(ResponseMessage));
    ResponseMessage.BodyLength = ResponseBufferSize;
    ResponseMessage.Body = ResponseBuffer;

    OUT_PRINT(L"Creating response event...\n");
    Status = gBS->CreateEvent(
        EVT_NOTIFY_SIGNAL,
        TPL_CALLBACK,
        RequestEvent,
        (VOID*) NULL,
        &ResponseEvent
    );

    if (EFI_ERROR(Status)) {
        gBS->CloseEvent(ResponseEvent);
        return Status;
    }

    ZeroMem(&ResponseToken, sizeof(EFI_HTTP_TOKEN));
    ResponseToken.Event = ResponseEvent;
    ResponseToken.Message = &ResponseMessage;

    // Receive the response
    OUT_PRINT(L"Receiving response...\n");
    Status = Http->Response(Http, &ResponseToken);
    if (EFI_ERROR(Status)) {
        gBS->CloseEvent(ResponseEvent);
        return Status;
    }

    // Wait for the response event to be signaled indicating the response is ready
    OUT_PRINT(L"Waiting for response...\n");
    gBS->WaitForEvent(1, &ResponseEvent, NULL);

    // Process the response
    OUT_PRINT(L"Response: %a\n", ResponseBuffer);
    OUT_PRINT(L"DONE.");

    gBS->CloseEvent(ResponseEvent);
    return EFI_SUCCESS;
}

VOID VCAuth2FASampleRequest()
{
    EFI_STATUS Status;
    EFI_HTTP_PROTOCOL* Http = NULL;
    EFI_IPv4_ADDRESS ServerIp;

    ServerIp.Addr[0] = 192;
    ServerIp.Addr[1] = 168;
    ServerIp.Addr[2] = 1;
    ServerIp.Addr[3] = 120;

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
    
    OUT_PRINT(L"Accessing server resource...\n");
    Status = AccessServerResource(Http, &ServerIp, 8080);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to access server resource: %r\n", Status);
        return;
    }
    while (TRUE) {} 
}

