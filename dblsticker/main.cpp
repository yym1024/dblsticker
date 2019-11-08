
#include <Windows.h>
#include <WindowsX.h>
#include <CommCtrl.h>
#include <Shlwapi.h>
#include <wincodec.h>
#include <intrin.h>
#include "resource.h"

// 创建 WIC 工厂
STDAPI WICCreateImagingFactory_Proxy(
	_In_  UINT               SDKVersion,
	_Out_ IWICImagingFactory **ppIImagingFactory
	);

#pragma comment(lib, "ComCtl32")
#pragma comment(lib, "Shlwapi")
#pragma comment(lib, "WindowsCodecs")

// 系统控件主题支持
#if defined _M_IX86
#pragma comment(linker,"/manifestdependency:\"" \
"type='win32' " \
"name='Microsoft.Windows.Common-Controls' " \
"version='6.0.0.0' processorArchitecture='x86' " \
"publicKeyToken='6595b64144ccf1df' " \
"language='*'" \
"\"")
#elif defined _M_X64
#pragma comment(linker,"/manifestdependency:\"" \
"type='win32' " \
"name='Microsoft.Windows.Common-Controls' " \
"version='6.0.0.0' processorArchitecture='amd64' " \
"publicKeyToken='6595b64144ccf1df' " \
"language='*'" \
"\"")
#elif defined _M_ARM
#pragma comment(linker,"/manifestdependency:\"" \
"type='win32' " \
"name='Microsoft.Windows.Common-Controls' " \
"version='6.0.0.0' processorArchitecture='arm' " \
"publicKeyToken='6595b64144ccf1df' " \
"language='*'" \
"\"")
#else
#pragma comment(linker,"/manifestdependency:\""
"type='win32' "
"name='Microsoft.Windows.Common-Controls' "
"version='6.0.0.0' processorArchitecture='*' "
"publicKeyToken='6595b64144ccf1df' "
"language='*'"
"\"")
#endif

// Release模式优化
#ifdef NDEBUG
static HANDLE hHeap;
//FORCEINLINE void *malloc(size_t size) { return HeapAlloc(hHeap, 0, size); }
//FORCEINLINE void *calloc(size_t count, size_t size) { return HeapAlloc(hHeap, HEAP_ZERO_MEMORY, count * size); }
//FORCEINLINE size_t _msize(void *block) { return HeapSize(hHeap, 0, block); }
//FORCEINLINE void free(void *block) { HeapFree(hHeap, 0, block); }
FORCEINLINE void *MyMalloc(size_t size) { return HeapAlloc(hHeap, 0, size); }
FORCEINLINE void MyFree(void *block) { HeapFree(hHeap, 0, block); }
#pragma comment(linker, "/ENTRY:mainEntry")
EXTERN_C void mainEntry()
{
	extern int main();
	hHeap = GetProcessHeap();
	ExitProcess(main());
}
#else
FORCEINLINE void *MyMalloc(size_t size) { return malloc(size); }
FORCEINLINE void MyFree(void *block) { free(block); }
#pragma comment(linker, "/ENTRY:mainCRTStartup")
#endif // NDEBUG

// 如果失败就抛出COM异常
#define FAILED_RETURN(_HRESULT_, _EXPRESSIONS_) if (FAILED(_HRESULT_ = _EXPRESSIONS_)) return _HRESULT_

// 当前模块的镜像基址
EXTERN_C HINSTANCE__ __ImageBase;
// 对话框消息函数
static INT_PTR CALLBACK MainProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// 位图信息
typedef struct WICBmpInfo
{
	//const WICPixelFormatGUID *format;
	SIZE size;
	UINT /*stride,*/ bytes;
	BYTE buffer[];
	FORCEINLINE void free() const { ::MyFree(PVOID(this)); }
} WICBmpInfo, *PWICBmpInfo;
typedef const WICBmpInfo *PCWICBmpInfo;

// 自动管理COM指针
template <typename T>
class AutoComPtr
{
	T *p;
public:

	FORCEINLINE AutoComPtr() : p() {}
	//FORCEINLINE AutoComPtr(std::nullptr_t) : p() {}
	FORCEINLINE AutoComPtr(T *pUnk) : p(pUnk) { if (pUnk) static_cast<IUnknown*>(pUnk)->AddRef(); }
	FORCEINLINE AutoComPtr(IUnknown *pUnk) : p() { if (pUnk) pUnk->QueryInterface(&p); }
	FORCEINLINE AutoComPtr(const AutoComPtr &other) : p(other) { p->AddRef(); }
	FORCEINLINE ~AutoComPtr() { if (p) p->Release(); }
	//FORCEINLINE AutoComPtr &operator=(std::nullptr_t) { this->~AutoComPtr(); new(this) AutoComPtr(); }
	FORCEINLINE AutoComPtr &operator=(T *pUnk) { this->~AutoComPtr(); new(this) AutoComPtr(pUnk); }
	FORCEINLINE AutoComPtr &operator=(IUnknown *pUnk) { this->~AutoComPtr(); new(this) AutoComPtr(pUnk); }
	FORCEINLINE AutoComPtr &operator=(const AutoComPtr &other) { this->~AutoComPtr(); new(this) AutoComPtr(other); }
	FORCEINLINE T **operator&() { return &p; }
	FORCEINLINE operator T*() { return p; }
	FORCEINLINE T *operator->() { return p; }
	FORCEINLINE T &operator*() { return *p; }
};

// 工厂实例
static IWICImagingFactory *pWICFac;
// 加载位图
static HRESULT LoadWICBitmap(LPCWSTR pFileName, PCWICBmpInfo *ppBmpInfo/*, const WICPixelFormatGUID *pixelFormat = nullptr*/);
// 保存位图
static HRESULT SaveWICBitmap(IWICBitmap *pBmp, LPCWSTR pFileName, REFGUID rEncoder, WICPixelFormatGUID *pPixelFormat = nullptr);
// 合成位图
static HRESULT MakeWICBitmap(LPCWSTR pFileName, REFGUID rEncoder, LPCBYTE pbuf1, LPCBYTE pbuf2, UINT width, UINT height);
// 异常输出
static void OutputHResult(HWND hEdit, HRESULT hrCode);

int main()
{
	// 初始化通用控件
	InitCommonControls();
	// 加载对话框资源
	return DialogBox(&__ImageBase, MAKEINTRESOURCE(IDD_MAINVIEW), HWND_DESKTOP, MainProc);
}

INT_PTR CALLBACK MainProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static PCWICBmpInfo pwbis[2];
	switch (uMsg)
	{
		// 初始化对话框
	case WM_INITDIALOG:
		WICCreateImagingFactory_Proxy(WINCODEC_SDK_VERSION, &pWICFac);
		Edit_LimitText(GetDlgItem(hWnd, IDC_MSGOUT), 0);
		break;
		// 销毁对话框
	case WM_DESTROY:
		for each(PCWICBmpInfo cur in pwbis)
			cur->free();
		pWICFac->Release();
		break;
		// 关闭对话框
	case WM_CLOSE:
		return EndDialog(hWnd, EXIT_SUCCESS);
		// 控件消息
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_BtnPic1:
		case IDC_BtnPic2:
		{
			WCHAR filename[MAX_PATH];
			static OPENFILENAMEW ofn = {
				sizeof(ofn), hWnd, &__ImageBase,
				L"Device-Independent Bitmap(*.bmp;*.dib)\0*.bmp;*.dib\0"
				L"Portable Network Graphics(*.png)\0*.png\0"
				L"Windows Icon&Cursor(*.ico;*.cur)\0*.ico;*.cur\0"
				L"Joint Photographic Experts Group(*.jpg;*.jpeg)\0*.jpg;*.jpeg\0"
				L"Tag Image File Format(*.tif;*.tiff)\0*.tif;*.tiff\0"
				L"Graphics Interchange Format(*.gif)\0*.gif\0"
				L"Windows Media HD Photo(*.wdp;*.wmp;*.hdp)\0*.wdp;*.wmp;*.hdp\0"
				L"All WIC Format(*.bmp;*.dib;*.png;*.ico;*.cur;*.jpg;*.jpeg;*.tif;*.tiff;*.gif;*.wdp;*.wmp;*.hdp)\0*.bmp;*.dib;*.png;*.ico;*.cur;*.jpg;*.jpeg;*.tif;*.tiff;*.gif;*.wdp;*.wmp;*.hdp\0"
				L"All File Format\0*\0"
				, nullptr, 0, 8, nullptr, ARRAYSIZE(filename),
				nullptr, 0, nullptr, nullptr,
				OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_READONLY,
			};
			//*filename = L'\0';
			HWND hEdit = GetDlgItem(hWnd, LOWORD(wParam) - 1);
			GetWindowTextW(hEdit, filename, ARRAYSIZE(filename));
			ofn.lpstrFile = filename;
			if (GetOpenFileNameW(&ofn))
			{
				SetWindowTextW(hEdit, filename);
				PCWICBmpInfo &rwbi = pwbis[(LOWORD(wParam) - IDC_InPic1) >> 1];
				rwbi->free();
				HRESULT hr = LoadWICBitmap(filename, &rwbi);
				HWND hSynthe = GetDlgItem(hWnd, IDC_BtnSynthe);
				HWND hOutmsg = GetDlgItem(hWnd, IDC_MSGOUT);
				if (FAILED(hr))
				{
					OutputHResult(hOutmsg, hr);
				}
				else if (pwbis[0] && pwbis[1])
				{
					if (pwbis[0]->size.cx == pwbis[1]->size.cx &&
						pwbis[0]->size.cy == pwbis[1]->size.cy)
					{
						EnableWindow(hSynthe, TRUE);
						break;
					}
					OutputHResult(hOutmsg, -1);
				}
				EnableWindow(hSynthe, FALSE);
			}
		}
		break;
		case IDC_BtnSynthe:
		{
			const LPCGUID FileFormat[] = { &GUID_ContainerFormatPng, &GUID_ContainerFormatTiff, &GUID_ContainerFormatWmp, };
			static WCHAR filename[MAX_PATH];
			static OPENFILENAMEW ofn = {
				sizeof(ofn), hWnd, &__ImageBase,
				L"Portable Network Graphics(*.png)\0*.png\0"
				L"Tag Image File Format(*.tif;*.tiff)\0*.tif;*.tiff\0"
				L"Windows Media HD Photo(*.wdp;*.wmp;*.hdp)\0*.wdp;*.wmp;*.hdp\0"
				, nullptr, 0, 1, filename, ARRAYSIZE(filename),
				nullptr, 0, nullptr, nullptr,
				OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT,
				0, 0, L"png",
			};
			//*filename = L'\0';
			if (GetSaveFileNameW(&ofn))
			{
				OutputHResult(GetDlgItem(hWnd, IDC_MSGOUT), MakeWICBitmap(
					filename, *FileFormat[ofn.nFilterIndex - 1], pwbis[0]->buffer,
					pwbis[1]->buffer, pwbis[0]->size.cx, pwbis[0]->size.cy));
			}
		}
		break;
		}
		break;
		// 其他消息
	default:
		return FALSE;
	}
	return TRUE;
}

HRESULT LoadWICBitmap(LPCWSTR pFileName, PCWICBmpInfo *ppBmpInfo/*, const WICPixelFormatGUID *pixelFormat*/)
{
	HRESULT hr;
	AutoComPtr<IWICBitmapDecoder> pBDc;
	FAILED_RETURN(hr, pWICFac->CreateDecoderFromFilename(pFileName, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &pBDc));
	AutoComPtr<IWICBitmapFrameDecode> pBFD;
	FAILED_RETURN(hr, pBDc->GetFrame(0, &pBFD));
	AutoComPtr<IWICFormatConverter> pFmtCon;
	FAILED_RETURN(hr, pWICFac->CreateFormatConverter(&pFmtCon));
	FAILED_RETURN(hr, pFmtCon->Initialize(pBFD, GUID_WICPixelFormat8bppGray, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));
	UINT width, height;
	FAILED_RETURN(hr, pBFD->GetSize(&width, &height));
	PWICBmpInfo pwbi = PWICBmpInfo(MyMalloc(sizeof(**ppBmpInfo) + width * height * 1));
	if (!pwbi)
		return E_OUTOFMEMORY;
	//pwbi->format = pixelFormat;
	pwbi->size.cx = width;
	pwbi->size.cy = height;
	//pwbi->stride = 1 * width;
	pwbi->bytes = width * height;
	if (FAILED(hr = pFmtCon->CopyPixels(nullptr, width, pwbi->bytes, pwbi->buffer)))
	{
		*ppBmpInfo = nullptr;
		pwbi->free();
		return hr;
	}
	*ppBmpInfo = pwbi;
	return S_OK;
}

HRESULT SaveWICBitmap(IWICBitmap *pBmp, LPCWSTR pFileName, REFGUID rEncoder, WICPixelFormatGUID *pPixelFormat)
{
	HRESULT hr;
	AutoComPtr<IWICStream> pStm;
	FAILED_RETURN(hr, pWICFac->CreateStream(&pStm));
	FAILED_RETURN(hr, pStm->InitializeFromFilename(pFileName, GENERIC_WRITE));
	AutoComPtr<IWICBitmapEncoder> pBEc;
	FAILED_RETURN(hr, pWICFac->CreateEncoder(rEncoder, nullptr, &pBEc));
	AutoComPtr<IWICBitmapFrameEncode> pBFE;
	FAILED_RETURN(hr, pBEc->Initialize(pStm, WICBitmapEncoderNoCache));
	FAILED_RETURN(hr, pBEc->CreateNewFrame(&pBFE, nullptr));
	FAILED_RETURN(hr, pBFE->Initialize(nullptr));
	if (pPixelFormat)
		FAILED_RETURN(hr, pBFE->SetPixelFormat(pPixelFormat));
	FAILED_RETURN(hr, pBFE->WriteSource(pBmp, nullptr));
	FAILED_RETURN(hr, pBFE->Commit());
	FAILED_RETURN(hr, pBEc->Commit());
	return S_OK;
}

HRESULT MakeWICBitmap(LPCWSTR pFileName, REFGUID rEncoder, LPCBYTE pbuf1, LPCBYTE pbuf2, UINT width, UINT height)
{
	HRESULT hr;
	AutoComPtr<IWICBitmap> pBmp;
	FAILED_RETURN(hr, pWICFac->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &pBmp));
	{
		AutoComPtr<IWICBitmapLock> pLock;
		FAILED_RETURN(hr, pBmp->Lock(nullptr, WICBitmapLockWrite, &pLock));
		UINT size, stride;
		WICInProcPointer pdat;
		FAILED_RETURN(hr, pLock->GetDataPointer(&size, &pdat));
		FAILED_RETURN(hr, pLock->GetStride(&stride));
		for (UINT v = 0; v < height; ++v)
		{
			UINT i = width * v;
			UINT j = stride * v;
			for (UINT u = 0; u < width; ++u, ++i, j += 4)
			{
				UINT src = pbuf1[i] >> 1;
				UINT dst = pbuf2[i] >> 1;
				UINT alpha = 0x80 + dst - src;
				*PUINT32(pdat + j) = (alpha << 24) | RGB(dst, dst, dst);
			}
		}
	}
	return SaveWICBitmap(pBmp, pFileName, rEncoder);
}

#define OUTPUTSTR(BUFFER, STRING) __movsw(PWORD(buf), PWORD(L##STRING), min(ARRAYSIZE(BUFFER), ARRAYSIZE(L##STRING)))
void OutputHResult(HWND hEdit, HRESULT hrCode)
{
	WCHAR buf[0x1000];
	if (SUCCEEDED(hrCode))
		return;
	switch (hrCode)
	{
	case -1:
		OUTPUTSTR(buf, "两张图片尺寸必须相同！\r\n");
		break;
	case WINCODEC_ERR_WRONGSTATE:
		OUTPUTSTR(buf, "编解码器处于异常状态！\r\n");
		break;
	case WINCODEC_ERR_UNKNOWNIMAGEFORMAT:
		OUTPUTSTR(buf, "未知图像格式！\r\n");
		break;
	case WINCODEC_ERR_UNSUPPORTEDVERSION:
		OUTPUTSTR(buf, "系统WIC组件版本太低，请及时更新！\r\n");
		break;
	case WINCODEC_ERR_PROPERTYNOTFOUND:
		OUTPUTSTR(buf, "找不到指定的图像属性！\r\n");
		break;
	case WINCODEC_ERR_PROPERTYNOTSUPPORTED:
		OUTPUTSTR(buf, "编解码器不支持属性！\r\n");
		break;
	case WINCODEC_ERR_PROPERTYSIZE:
		OUTPUTSTR(buf, "图像属性大小无效！\r\n");
		break;
	case WINCODEC_ERR_PALETTEUNAVAILABLE:
		OUTPUTSTR(buf, "无效调色板！\r\n");
		break;
	case WINCODEC_ERR_INTERNALERROR:
		OUTPUTSTR(buf, "系统WIC组件内部错误！\r\n");
		break;
	case WINCODEC_ERR_SOURCERECTDOESNOTMATCHDIMENSIONS:
		OUTPUTSTR(buf, "图像边界与尺寸不匹配！\r\n");
		break;
	case WINCODEC_ERR_COMPONENTNOTFOUND:
		OUTPUTSTR(buf, "无法找到相应的组件（或编解码器），可能是不支持的文件格式！\r\n");
		break;
	case WINCODEC_ERR_IMAGESIZEOUTOFRANGE:
		OUTPUTSTR(buf, "图片大小超出范围！\r\n");
		break;
	case WINCODEC_ERR_BADIMAGE:
		OUTPUTSTR(buf, "无效图片文件！\r\n");
		break;
	case WINCODEC_ERR_BADHEADER:
		OUTPUTSTR(buf, "无法图像数据头！\r\n");
		break;
	case WINCODEC_ERR_FRAMEMISSING:
		OUTPUTSTR(buf, "图像帧丢失！\r\n");
		break;
	case WINCODEC_ERR_BADMETADATAHEADER:
		OUTPUTSTR(buf, "无法图像元数据头！\r\n");
		break;
	case WINCODEC_ERR_BADSTREAMDATA:
		OUTPUTSTR(buf, "无效数据流！\r\n");
		break;
	case WINCODEC_ERR_STREAMWRITE:
		OUTPUTSTR(buf, "写入数据流失败！\r\n");
		break;
	case WINCODEC_ERR_STREAMREAD:
		OUTPUTSTR(buf, "读取数据流失败！\r\n");
		break;
	case WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT:
		OUTPUTSTR(buf, "不支持的像素格式！\r\n");
		break;
	case WINCODEC_ERR_UNSUPPORTEDOPERATION:
		OUTPUTSTR(buf, "非法操作！\r\n");
		break;
	case WINCODEC_ERR_INVALIDREGISTRATION:
		OUTPUTSTR(buf, "组件（或编解码器）注册无效，请重新安装相应组件！\r\n");
		break;
	case WINCODEC_ERR_COMPONENTINITIALIZEFAILURE:
		OUTPUTSTR(buf, "组件（或编解码器）初始化失败！\r\n");
		break;
	case WINCODEC_ERR_DUPLICATEMETADATAPRESENT:
		OUTPUTSTR(buf, "元数据重复！\r\n");
		break;
	case WINCODEC_ERR_PROPERTYUNEXPECTEDTYPE:
		OUTPUTSTR(buf, "图像属性无效！\r\n");
		break;
	case WINCODEC_ERR_UNEXPECTEDSIZE:
		OUTPUTSTR(buf, "大小异常！\r\n");
		break;
	case WINCODEC_ERR_INVALIDQUERYREQUEST:
		OUTPUTSTR(buf, "获取图像属性失败！\r\n");
		break;
	case WINCODEC_ERR_UNEXPECTEDMETADATATYPE:
		OUTPUTSTR(buf, "元数据无效！\r\n");
		break;
	case WINCODEC_ERR_WIN32ERROR:
		OUTPUTSTR(buf, "系统错误！\r\n");
		break;
	case WINCODEC_ERR_ACCESSDENIED:
		OUTPUTSTR(buf, "权限不足！\r\n");
		break;
	case WINCODEC_ERR_OUTOFMEMORY:
		OUTPUTSTR(buf, "内存不足！\r\n");
		break;
	default:
		wnsprintfW(buf, ARRAYSIZE(buf), L"未知错误（0x%08X)！\r\n", hrCode);
	}
	SendMessageW(hEdit, EM_SETSEL, -1, -1);
	SendMessageW(hEdit, EM_REPLACESEL, 0, LPARAM(buf));
}
