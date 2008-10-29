#if !defined(OSX64)
#include <wx/aboutdlg.h>
#endif

#include "Common.h"
#include "StringUtil.h"

#include "pluginspecs_wiimote.h"

#include "wiimote_hid.h"

SWiimoteInitialize g_WiimoteInitialize;
#define VERSION_STRING "0.1"

//******************************************************************************
// Definitions and variable declarations
//******************************************************************************

//libogc bounding box, in smoothed IR coordinates: 232,284 792,704
//we'll use it to scale our mouse coordinates
#define LEFT 232
#define TOP 284
#define RIGHT 792
#define BOTTOM 704
#define SENSOR_BAR_RADIUS 200

// vars 
#define WIIMOTE_EEPROM_SIZE (16*1024)
#define WIIMOTE_REG_SPEAKER_SIZE 10
#define WIIMOTE_REG_EXT_SIZE 0x100
#define WIIMOTE_REG_IR_SIZE 0x34

u8 g_Leds = 0x1;

u8 g_Eeprom[WIIMOTE_EEPROM_SIZE];

u8 g_RegSpeaker[WIIMOTE_REG_SPEAKER_SIZE];
u8 g_RegExt[WIIMOTE_REG_EXT_SIZE];
u8 g_RegIr[WIIMOTE_REG_IR_SIZE];

u8 g_ReportingMode;
u16 g_ReportingChannel;

static const u8 EepromData_0[] = {
	0xA1, 0xAA, 0x8B, 0x99, 0xAE, 0x9E, 0x78, 0x30,
	0xA7, 0x74, 0xD3, 0xA1, 0xAA, 0x8B, 0x99, 0xAE,
	0x9E, 0x78, 0x30, 0xA7, 0x74, 0xD3, 0x82, 0x82,
	0x82, 0x15, 0x9C, 0x9C, 0x9E, 0x38, 0x40, 0x3E,
	0x82, 0x82, 0x82, 0x15, 0x9C, 0x9C, 0x9E, 0x38,
	0x40, 0x3E
};

static const u8 EepromData_16D0[] = {
	0x00, 0x00, 0x00, 0xFF, 0x11, 0xEE, 0x00, 0x00,
	0x33, 0xCC, 0x44, 0xBB, 0x00, 0x00, 0x66, 0x99,
	0x77, 0x88, 0x00, 0x00, 0x2B, 0x01, 0xE8, 0x13
};

//******************************************************************************
// Subroutine declarations
//******************************************************************************
void __Log(int log, const char *format, ...)
{
	char* temp = (char*)alloca(strlen(format)+512);
	va_list args;
	va_start(args, format);
	CharArrayFromFormatV(temp, 512, format, args);
	va_end(args);
	g_WiimoteInitialize.pLog(temp);
}
//void PanicAlert(const char* fmt, ...);

void HidOutputReport(u16 _channelID, wm_report* sr);

void WmLeds(u16 _channelID, wm_leds* leds);
void WmReadData(u16 _channelID, wm_read_data* rd);
void WmWriteData(u16 _channelID, wm_write_data* wd);
void WmRequestStatus(u16 _channelID, wm_request_status* rs);
void WmDataReporting(u16 _channelID, wm_data_reporting* dr);

void SendReadDataReply(u16 _channelID, void* _Base, u16 _Address, u8 _Size);
void SendReportCoreAccel(u16 _channelID);
void SendReportCoreAccelIr12(u16 _channelID);
void SendReportCore(u16 _channelID);
void SendReportCoreAccelIr10Ext(u16 _channelID);

int WriteWmReport(u8* dst, u8 channel);
void WmSendAck(u16 _channelID, u8 _reportID);

static u32 convert24bit(const u8* src) {
	return (src[0] << 16) | (src[1] << 8) | src[2];
}

static u16 convert16bit(const u8* src) {
	return (src[0] << 8) | src[1];
}
#ifdef _WIN32
HINSTANCE g_hInstance;

void GetMousePos(float& x, float& y)
{
#ifdef _WIN32
	POINT point;

	GetCursorPos(&point);
	ScreenToClient(g_WiimoteInitialize.hWnd, &point);

	RECT Rect;
	GetClientRect(g_WiimoteInitialize.hWnd, &Rect);

	int width = Rect.right - Rect.left;
	int height = Rect.bottom - Rect.top;

	x = point.x / (float)width;
	y = point.y / (float)height;
#else
	x = 0.5f;
	y = 0.5f;
#endif
}



class wxDLLApp : public wxApp
{
	bool OnInit()
	{
		return true;
	}
};
IMPLEMENT_APP_NO_MAIN(wxDLLApp) 

WXDLLIMPEXP_BASE void wxSetInstance(HINSTANCE hInst);


BOOL APIENTRY DllMain(HINSTANCE hinstDLL,	// DLL module handle
					  DWORD dwReason,		// reason called
					  LPVOID lpvReserved)	// reserved
{
	switch (dwReason)
	{
	case DLL_PROCESS_ATTACH:
		{       //use wxInitialize() if you don't want GUI instead of the following 12 lines
			wxSetInstance((HINSTANCE)hinstDLL);
			int argc = 0;
			char **argv = NULL;
			wxEntryStart(argc, argv);
			if ( !wxTheApp || !wxTheApp->CallOnInit() )
				return FALSE;
		}
		break; 

	case DLL_PROCESS_DETACH:
		wxEntryCleanup(); //use wxUninitialize() if you don't want GUI 
		break;
	default:
		break;
	}

	g_hInstance = hinstDLL;
	return TRUE;
}
#endif
//******************************************************************************
// Exports
//******************************************************************************
extern "C" void GetDllInfo (PLUGIN_INFO* _PluginInfo) 
{
	_PluginInfo->Version = 0x0100;
	_PluginInfo->Type = PLUGIN_TYPE_WIIMOTE;
#ifdef DEBUGFAST 
	sprintf(_PluginInfo->Name, "Wiimote Test (DebugFast) " VERSION_STRING);
#else
#ifndef _DEBUG
	sprintf(_PluginInfo->Name, "Wiimote Test " VERSION_STRING);
#else
	sprintf(_PluginInfo->Name, "Wiimote Test (Debug) " VERSION_STRING);
#endif
#endif
}


extern "C" void DllAbout(HWND _hParent) 
{
#if !defined(OSX64)
	wxAboutDialogInfo info;
	info.SetName(_T("Wiimote test plugin"));
	info.AddDeveloper(_T("masken (masken3@gmail.com)"));
	info.SetDescription(_T("Wiimote test plugin"));
	wxAboutBox(info);
#endif
}

extern "C" void DllConfig(HWND _hParent)
{
}

void CryptBuffer(u8* _buffer, u8 _size)
{
	for (int i=0; i<_size; i++)
	{
		_buffer[i] = ((_buffer[i] - 0x17) ^ 0x17) & 0xFF;
	}
}

void WriteCryped16(u8* _baseBlock, u16 _address, u16 _value)
{
	u16 cryptedValue = _value;
	CryptBuffer((u8*)&cryptedValue, sizeof(u16));

	*(u16*)(_baseBlock + _address) = cryptedValue;
}

extern "C" void Wiimote_Initialize(SWiimoteInitialize _WiimoteInitialize)
{
	g_WiimoteInitialize = _WiimoteInitialize;

	memset(g_Eeprom, 0, WIIMOTE_EEPROM_SIZE);
	memcpy(g_Eeprom, EepromData_0, sizeof(EepromData_0));
	memcpy(g_Eeprom + 0x16D0, EepromData_16D0, sizeof(EepromData_16D0));

	g_ReportingMode = 0;


	
	WriteCryped16(g_RegExt, 0xfe, 0x0000);

//	g_RegExt[0xfd] = 0x1e;
//	g_RegExt[0xfc] = 0x9a;

}

extern "C" void Wiimote_DoState(void* ptr, int mode) 
{
	//TODO: implement
}

extern "C" void Wiimote_Shutdown(void) 
{
}

extern "C" void Wiimote_Input(u16 _channelID, const void* _pData, u32 _Size) 
{
	
	const u8* data = (const u8*)_pData;

	// dump raw data
	{
		LOG(WIIMOTE, "Wiimote_Input");
		std::string Temp;
		for (u32 j=0; j<_Size; j++)
		{
			char Buffer[128];
			sprintf(Buffer, "%02x ", data[j]);
			Temp.append(Buffer);
		}
		LOG(WIIMOTE, "   Data: %s", Temp.c_str());
	}
	hid_packet* hidp = (hid_packet*) data;

	switch(hidp->type)
	{
	case HID_TYPE_DATA:
		{
			switch(hidp->param)
			{
			case HID_PARAM_OUTPUT:
				{
					wm_report* sr = (wm_report*)hidp->data;
					WmSendAck(_channelID, sr->channel);
					HidOutputReport(_channelID, sr);
				}
				break;

			default:
				PanicAlert("HidInput: HID_TYPE_DATA - param 0x%02x", hidp->type, hidp->param);
				break;
			}
		}
		break;

	default:
		PanicAlert("HidInput: Unknown type 0x%02x and param 0x%02x", hidp->type, hidp->param);
		break;
	}
}

extern "C" void Wiimote_Output(u16 _channelID, const void* _pData, u32 _Size) 
{
	const u8* data = (const u8*)_pData;
	// dump raw data
	{
		LOG(WIIMOTE, "Wiimote_Output");
		std::string Temp;
		for (u32 j=0; j<_Size; j++)
		{
			char Buffer[128];
			sprintf(Buffer, "%02x ", data[j]);
			Temp.append(Buffer);
		}
		LOG(WIIMOTE, "   Data: %s", Temp.c_str());
	}

	hid_packet* hidp = (hid_packet*) data;
	PanicAlert("HidOutput: Unknown type %x and param %x", hidp->type, hidp->param);

	switch(hidp->type)
	{
	case HID_TYPE_HANDSHAKE:
		if (hidp->param == HID_PARAM_INPUT)
		{
			PanicAlert("HID_TYPE_HANDSHAKE - HID_PARAM_INPUT");
		}
		else
		{
			PanicAlert("HID_TYPE_HANDSHAKE - HID_PARAM_OUTPUT");
		}
		break;

	case HID_TYPE_SET_REPORT:
		if (hidp->param == HID_PARAM_INPUT)
		{
			PanicAlert("HID_TYPE_SET_REPORT input");
		}
		else
		{
			HidOutputReport(_channelID, (wm_report*)hidp->data);
		}
		break;

	case HID_TYPE_DATA:
		PanicAlert("HID_TYPE_DATA %s", hidp->type, hidp->param == HID_PARAM_INPUT ? "input" : "output");
		break;

	default:
		PanicAlert("HidOutput: Unknown type %x and param %x", hidp->type, hidp->param);
		break;
	}

}

extern "C" void Wiimote_Update() {
	//LOG(WIIMOTE, "Wiimote_Update");

	switch(g_ReportingMode) {
	case 0:
		break;
	case WM_REPORT_CORE:			SendReportCore(g_ReportingChannel);			break;
	case WM_REPORT_CORE_ACCEL:		SendReportCoreAccel(g_ReportingChannel);	break;
	case WM_REPORT_CORE_ACCEL_IR12: SendReportCoreAccelIr12(g_ReportingChannel);break;
	case WM_REPORT_CORE_ACCEL_IR10_EXT6: SendReportCoreAccelIr10Ext(g_ReportingChannel);break;
	}
	// g_ReportingMode = 0;
}

extern "C" unsigned int Wiimote_GetAttachedControllers() {
	return 1;
}

//******************************************************************************
// Subroutines
//******************************************************************************
void HidOutputReport(u16 _channelID, wm_report* sr) {
	LOG(WIIMOTE, "  HidOutputReport(0x%02x)", sr->channel);

	switch(sr->channel)
	{
	case WM_LEDS:
		WmLeds(_channelID, (wm_leds*)sr->data);
		break;
	case WM_READ_DATA:
		WmReadData(_channelID, (wm_read_data*)sr->data);
		break;
	case WM_REQUEST_STATUS:
		WmRequestStatus(_channelID, (wm_request_status*)sr->data);
		break;
	case WM_IR_PIXEL_CLOCK:
	case WM_IR_LOGIC:
		LOG(WIIMOTE, " IR Enable 0x%02x 0x%02x", sr->channel, sr->data[0]);
		break;
	case WM_WRITE_DATA:
		WmWriteData(_channelID, (wm_write_data*)sr->data);
		break;
	case WM_DATA_REPORTING:
		WmDataReporting(_channelID, (wm_data_reporting*)sr->data);
		break;

	case WM_SPEAKER_ENABLE:
		LOG(WIIMOTE, " WM Speaker Enable 0x%02x 0x%02x", sr->channel, sr->data[0]);
		break;

	case WM_SPEAKER_MUTE:
		LOG(WIIMOTE, " WM Mute Enable 0x%02x 0x%02x", sr->channel, sr->data[0]);
		break;

	default:
		PanicAlert("HidOutputReport: Unknown channel 0x%02x", sr->channel);
		return;
	}
}

void WmLeds(u16 _channelID, wm_leds* leds) {
	LOG(WIIMOTE, " Set LEDs");
	LOG(WIIMOTE, "  Leds: %x", leds->leds);
	LOG(WIIMOTE, "  Rumble: %x", leds->rumble);

	g_Leds = leds->leds;
}
void WmSendAck(u16 _channelID, u8 _reportID)
{
	u8 DataFrame[1024];

	u32 Offset = 0;

	// header
	hid_packet* pHidHeader = (hid_packet*)(DataFrame + Offset);
	pHidHeader->type = HID_TYPE_DATA;
	pHidHeader->param = HID_PARAM_INPUT;
	Offset += sizeof(hid_packet);

	wm_acknowledge* pData = (wm_acknowledge*)(DataFrame + Offset);
	pData->Channel = WM_WRITE_DATA_REPLY;
	pData->unk0 = 0;
	pData->unk1 = 0;
	pData->reportID = _reportID;
	pData->errorID = 0;
	Offset += sizeof(wm_acknowledge);


	LOG(WIIMOTE, "  WMSendAck()");

	g_WiimoteInitialize.pWiimoteInput(_channelID, DataFrame, Offset);
}


void WmDataReporting(u16 _channelID, wm_data_reporting* dr) 
{
	LOG(WIIMOTE, " Set Data reporting mode");
	LOG(WIIMOTE, "  Rumble: %x", dr->rumble);
	LOG(WIIMOTE, "  Continuous: %x", dr->continuous);
	LOG(WIIMOTE, "  All The Time: %x (not only on data change)", dr->all_the_time);
	LOG(WIIMOTE, "  Rumble: %x", dr->rumble);
	LOG(WIIMOTE, "  Mode: 0x%02x", dr->mode);

	g_ReportingMode = dr->mode;
	g_ReportingChannel = _channelID;
	switch(dr->mode) {	//see Wiimote_Update()
	case WM_REPORT_CORE:
	case WM_REPORT_CORE_ACCEL:
	case WM_REPORT_CORE_ACCEL_IR12:
	case WM_REPORT_CORE_ACCEL_IR10_EXT6:
		break;
	default:
		PanicAlert("Wiimote: Unknown reporting mode 0x%x", dr->mode);
	}

	// WmSendAck(_channelID, WM_DATA_REPORTING);
}


void FillReportInfo(wm_core& _core)
{
	memset(&_core, 0x00, sizeof(wm_core));

#ifdef _WIN32
	_core.a = GetAsyncKeyState(VK_LBUTTON) ? 1 : 0;
	_core.b = GetAsyncKeyState(VK_RBUTTON) ? 1 : 0;
#else 
        // TODO: fill in
#endif
}

void FillReportAcc(wm_accel& _acc)
{
	_acc.x = 0x00;
	_acc.y = 0x00;
	_acc.z = 0x00;
}

void FillReportIR(wm_ir_extended& _ir0, wm_ir_extended& _ir1)
{
	memset(&_ir0, 0xFF, sizeof(wm_ir_extended));
	memset(&_ir1, 0xFF, sizeof(wm_ir_extended));

	float MouseX, MouseY;
	GetMousePos(MouseX, MouseY);

	int y0 = TOP + (MouseY * (BOTTOM - TOP));
	int y1 = TOP + (MouseY * (BOTTOM - TOP));

	int x0 = LEFT + (MouseX * (RIGHT - LEFT)) - SENSOR_BAR_RADIUS;
	int x1 = LEFT + (MouseX * (RIGHT - LEFT)) + SENSOR_BAR_RADIUS;

	x0 = 1023 - x0;
	_ir0.x = x0 & 0xFF;
	_ir0.y = y0 & 0xFF;
	_ir0.size = 10;
	_ir0.xHi = x0 >> 8;
	_ir0.yHi = y0 >> 8;

	x1 = 1023 - x1;
	_ir1.x = x1;
	_ir1.y = y1 & 0xFF;
	_ir1.size = 10;
	_ir1.xHi = x1 >> 8;
	_ir1.yHi = y1 >> 8;
}

void FillReportIRBasic(wm_ir_basic& _ir0, wm_ir_basic& _ir1)
{
	memset(&_ir0, 0xFF, sizeof(wm_ir_basic));
	memset(&_ir1, 0xFF, sizeof(wm_ir_basic));

	float MouseX, MouseY;
	GetMousePos(MouseX, MouseY);

	int y1 = TOP + (MouseY * (BOTTOM - TOP));
	int y2 = TOP + (MouseY * (BOTTOM - TOP));

	int x1 = LEFT + (MouseX * (RIGHT - LEFT)) - SENSOR_BAR_RADIUS;
	int x2 = LEFT + (MouseX * (RIGHT - LEFT)) + SENSOR_BAR_RADIUS;

	x1 = 1023 - x1;
	_ir0.x1 = x1 & 0xFF;
	_ir0.y1 = y1 & 0xFF;
	_ir0.x1High = (x1 >> 8) & 0x3;
	_ir0.y1High = (y1 >> 8) & 0x3;

	x2 = 1023 - x2;
	_ir1.x2 = x2 & 0xFF;
	_ir1.y2 = y2 & 0xFF;
	_ir1.x2High = (x2 >> 8) & 0x3;
	_ir1.y2High = (y2 >> 8) & 0x3;
}


void SendReportCore(u16 _channelID) 
{
	u8 DataFrame[1024];
	u32 Offset = WriteWmReport(DataFrame, WM_REPORT_CORE);

	wm_report_core* pReport = (wm_report_core*)(DataFrame + Offset);
	Offset += sizeof(wm_report_core);
	memset(pReport, 0, sizeof(wm_report_core));

	FillReportInfo(pReport->c);

	LOG(WIIMOTE, "  SendReportCore()");

	g_WiimoteInitialize.pWiimoteInput(_channelID, DataFrame, Offset);
}


void SendReportCoreAccelIr12(u16 _channelID) {
	u8 DataFrame[1024];
	u32 Offset = WriteWmReport(DataFrame, WM_REPORT_CORE_ACCEL_IR12);

	wm_report_core_accel_ir12* pReport = (wm_report_core_accel_ir12*)(DataFrame + Offset);
	Offset += sizeof(wm_report_core_accel_ir12);
	memset(pReport, 0, sizeof(wm_report_core_accel_ir12));
	
	FillReportInfo(pReport->c);
	FillReportAcc(pReport->a);
	FillReportIR(pReport->ir[0], pReport->ir[1]);

	LOG(WIIMOTE, "  SendReportCoreAccelIr12()");

	g_WiimoteInitialize.pWiimoteInput(_channelID, DataFrame, Offset);
}

void SendReportCoreAccelIr10Ext(u16 _channelID) 
{
	u8 DataFrame[1024];
	u32 Offset = WriteWmReport(DataFrame, WM_REPORT_CORE_ACCEL_IR10_EXT6);

	wm_report_core_accel_ir10_ext6* pReport = (wm_report_core_accel_ir10_ext6*)(DataFrame + Offset);
	Offset += sizeof(wm_report_core_accel_ir10_ext6);
	memset(pReport, 0, sizeof(wm_report_core_accel_ir10_ext6));

	FillReportInfo(pReport->c);
	FillReportAcc(pReport->a);
	FillReportIRBasic(pReport->ir[0], pReport->ir[1]);

	LOG(WIIMOTE, "  SendReportCoreAccelIr10Ext()");

	g_WiimoteInitialize.pWiimoteInput(_channelID, DataFrame, Offset);
}


void SendReportCoreAccel(u16 _channelID) 
{
	u8 DataFrame[1024];
	u32 Offset = WriteWmReport(DataFrame, WM_REPORT_CORE_ACCEL);

	wm_report_core_accel* pReport = (wm_report_core_accel*)(DataFrame + Offset);
	Offset += sizeof(wm_report_core_accel);
	memset(pReport, 0, sizeof(wm_report_core_accel));
	
	FillReportInfo(pReport->c);
	FillReportAcc(pReport->a);

	LOG(WIIMOTE, "  SendReportCoreAccel()");

	g_WiimoteInitialize.pWiimoteInput(_channelID, DataFrame, Offset);
}

void WmReadData(u16 _channelID, wm_read_data* rd) 
{
	u32 address = convert24bit(rd->address);
	u16 size = convert16bit(rd->size);
	LOG(WIIMOTE, " Read data");
	LOG(WIIMOTE, "  Address space: %x", rd->space);
	LOG(WIIMOTE, "  Address: 0x%06x", address);
	LOG(WIIMOTE, "  Size: 0x%04x", size);
	LOG(WIIMOTE, "  Rumble: %x", rd->rumble);

	if(rd->space == 0) 
	{
		if (address + size > WIIMOTE_EEPROM_SIZE) 
		{
			PanicAlert("WmReadData: address + size out of bounds!");
			return;
		}
		SendReadDataReply(_channelID, g_Eeprom+address, address, (u8)size);
	} 
	else if(rd->space == WM_SPACE_REGS1 || rd->space == WM_SPACE_REGS2)
	{
		u8* block;
		u32 blockSize;
		switch((address >> 16) & 0xFE) 
		{
/*		case 0xA2:
			block = g_RegSpeaker;
			blockSize = WIIMOTE_REG_SPEAKER_SIZE;
			break;*/
		case 0xA4:
			block = g_RegExt;
			blockSize = WIIMOTE_REG_EXT_SIZE;
			PanicAlert("fsfsd");
			break;
/*		case 0xB0:
			block = g_RegIr;
			blockSize = WIIMOTE_REG_IR_SIZE;
			break;*/
		default:
			PanicAlert("WmWriteData: bad register block!");
			return;
		}
		address &= 0xFFFF;
		if(address + size > blockSize) {
			PanicAlert("WmReadData: address + size out of bounds!");
			return;
		}

		SendReadDataReply(_channelID, block+address, address, (u8)size);
	} 
	else
	{
		PanicAlert("WmReadData: unimplemented parameters (size: %i, addr: 0x%x!", size, rd->space);
	}
}

void WmWriteData(u16 _channelID, wm_write_data* wd) 
{
	u32 address = convert24bit(wd->address);
	LOG(WIIMOTE, " Write data");
	LOG(WIIMOTE, "  Address space: %x", wd->space);
	LOG(WIIMOTE, "  Address: 0x%06x", address);
	LOG(WIIMOTE, "  Size: 0x%02x", wd->size);
	LOG(WIIMOTE, "  Rumble: %x", wd->rumble);

	if(wd->size <= 16 && wd->space == WM_SPACE_EEPROM)
	{
		if(address + wd->size > WIIMOTE_EEPROM_SIZE) {
			PanicAlert("WmWriteData: address + size out of bounds!");
			return;
		}
		memcpy(g_Eeprom + address, wd->data, wd->size);

	//	WmSendAck(_channelID, WM_WRITE_DATA);
	}
	else if(wd->size <= 16 && (wd->space == WM_SPACE_REGS1 || wd->space == WM_SPACE_REGS2))
	{
		u8* block;
		u32 blockSize;
		switch((address >> 16) & 0xFE) {
		case 0xA2:
			block = g_RegSpeaker;
			blockSize = WIIMOTE_REG_SPEAKER_SIZE;
			break;
		case 0xA4:
			block = g_RegExt;
			blockSize = WIIMOTE_REG_EXT_SIZE;
			break;
		case 0xB0:
			block = g_RegIr;
			blockSize = WIIMOTE_REG_IR_SIZE;
			break;
		default:
			PanicAlert("WmWriteData: bad register block!");
			return;
		}
		address &= 0xFFFF;
		if(address + wd->size > blockSize) {
			PanicAlert("WmWriteData: address + size out of bounds!");
			return;
		}
		memcpy(wd->data, block + address, wd->size);

	} else {
		PanicAlert("WmWriteData: unimplemented parameters!");
	}
}

int WriteWmReport(u8* dst, u8 channel) {
	u32 Offset = 0;
	hid_packet* pHidHeader = (hid_packet*)(dst + Offset);
	Offset += sizeof(hid_packet);
	pHidHeader->type = HID_TYPE_DATA;
	pHidHeader->param = HID_PARAM_INPUT;

	wm_report* pReport = (wm_report*)(dst + Offset);
	Offset += sizeof(wm_report);
	pReport->channel = channel;
	return Offset;
}

void WmRequestStatus(u16 _channelID, wm_request_status* rs) {
	LOG(WIIMOTE, " Request Status");
	LOG(WIIMOTE, "  Rumble: %x", rs->rumble);

	//SendStatusReport();
	u8 DataFrame[1024];
	u32 Offset = WriteWmReport(DataFrame, WM_STATUS_REPORT);

	wm_status_report* pStatus = (wm_status_report*)(DataFrame + Offset);
	Offset += sizeof(wm_status_report);
	memset(pStatus, 0, sizeof(wm_status_report));
	pStatus->leds = g_Leds;
	pStatus->ir = 1;
	pStatus->battery = 0x4F;	//arbitrary number
	pStatus->extension = 0;

	LOG(WIIMOTE, "  SendStatusReport()");
	LOG(WIIMOTE, "    Flags: 0x%02x", pStatus->padding1[2]);
	LOG(WIIMOTE, "    Battery: %d", pStatus->battery);

	g_WiimoteInitialize.pWiimoteInput(_channelID, DataFrame, Offset);
}

void SendReadDataReply(u16 _channelID, void* _Base, u16 _Address, u8 _Size)
{
	int dataOffset = 0;
	while (_Size > 0)
	{
		u8 DataFrame[1024];
		u32 Offset = WriteWmReport(DataFrame, WM_READ_DATA_REPLY);
		
		int copySize = _Size;
		if (copySize > 16)
		{
			copySize = 16;
		}

		wm_read_data_reply* pReply = (wm_read_data_reply*)(DataFrame + Offset);
		Offset += sizeof(wm_read_data_reply);
		pReply->buttons = 0;
		pReply->error = 0;
		pReply->size = (copySize - 1) & 0xF;
		pReply->address = Common::swap16(_Address + dataOffset);
		memcpy(pReply->data + dataOffset, _Base, copySize);
		if(copySize < 16) 
		{
			memset(pReply->data + copySize, 0, 16 - copySize);
		}
		dataOffset += copySize;

		LOG(WIIMOTE, "  SendReadDataReply()");
		LOG(WIIMOTE, "    Buttons: 0x%04x", pReply->buttons);
		LOG(WIIMOTE, "    Error: 0x%x", pReply->error);
		LOG(WIIMOTE, "    Size: 0x%x", pReply->size);
		LOG(WIIMOTE, "    Address: 0x%04x", pReply->address);

		g_WiimoteInitialize.pWiimoteInput(_channelID, DataFrame, Offset);

		_Size -= copySize;
	}

	if (_Size != 0)
	{
		PanicAlert("WiiMote-Plugin: SendReadDataReply() failed");
	}
}
