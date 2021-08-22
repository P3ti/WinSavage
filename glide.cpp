#include <stdio.h>
#include <windows.h>
#include "patchutil.h"

typedef int (__stdcall *grSstWinOpen_t)(HWND, int, int, int, int, int, int);
typedef void (__stdcall *grSstWinClose_t)();

extern HWND GameWindow;
LRESULT CALLBACK GameWindowProc(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam);

extern const char* GlideProcNames[130];

grSstWinOpen_t grSstWinOpen = nullptr;
grSstWinClose_t grSstWinClose = nullptr;

static int __stdcall grSstWinOpenHook(HWND wnd, int res, int ref, int cfmt, int orgloc, int numbufs, int numauxbufs)
{
	if(!GameWindow)
	{
		HINSTANCE instance = GetModuleHandleW(NULL);

		static WNDCLASSW windowClass = { 0 };
		if(!windowClass.style)
		{
			windowClass.style = CS_OWNDC;
			windowClass.lpfnWndProc = GameWindowProc;
			windowClass.hInstance = GetModuleHandleW(NULL);
			windowClass.hCursor = LoadCursorW(NULL, IDC_ARROW);
			windowClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
			windowClass.lpszClassName = L"SQ";
			RegisterClassW(&windowClass);
		}

		DWORD windowStyle = WS_OVERLAPPED | WS_VISIBLE;
		RECT windowRect = { 0, 0, 640, 480 };
		AdjustWindowRect(&windowRect, windowStyle, FALSE);

		int windowWidth = windowRect.right - windowRect.left;
		int windowHeight = windowRect.bottom - windowRect.top;

		GameWindow = CreateWindowExW(0, windowClass.lpszClassName, L"Savage Quest", windowStyle, CW_USEDEFAULT, CW_USEDEFAULT, windowWidth, windowHeight, NULL, NULL, instance, NULL);
		ShowWindow(GameWindow, SW_SHOW);
	}

	wnd = GameWindow;

	printf("grSstWinOpen(%p, %d, %d, %d, %d, %d, %d)\n", wnd, res, ref, cfmt, orgloc, numbufs, numauxbufs);
	return grSstWinOpen(wnd, res, ref, cfmt, orgloc, numbufs, numauxbufs);
}

static void __stdcall grSstWinCloseHook()
{
	printf("grSstWinClose()\n");
	grSstWinClose();

    if(GameWindow)
    {
        DestroyWindow(GameWindow);
        GameWindow = NULL;
    }
}

bool PatchGlide2x(uintptr_t SQ)
{
	HMODULE glide2x = LoadLibraryA("GLIDE2X.DLL");
	if(!glide2x)
	{
		fprintf(stderr, "Failed to load glide2x.dll!\n");
		return false;
	}

	for(size_t i = 0; i < _countof(GlideProcNames); ++i)
	{
		FARPROC procAddress = GetProcAddress(glide2x, GlideProcNames[i]);
		if(!procAddress)
		{
			fprintf(stderr, "Glide proc %s missing!\n", GlideProcNames[i]);
			return false;
		}

		if(i == 75)
		{
			grSstWinOpen = (grSstWinOpen_t)procAddress;
			Hook(SQ + 0x1F3EC3, 0xE9, grSstWinOpenHook);
		}
		else if(i == 74)
		{
			grSstWinClose = (grSstWinClose_t)procAddress;
			Hook(SQ + 0x1F3EBE, 0xE9, grSstWinCloseHook);
		}
		else
		{
			Hook(SQ + 0x1F3D4C + i * 5, 0xE9, (uintptr_t)procAddress);
		}
	}

	return true;
}

const char* GlideProcNames[130] =
{
	"_grAADrawLine@8",
	"_grAADrawPoint@4",
	"_grAADrawPolygon@12",
	"_grDrawPlanarPolygonVertexList@8",
	"_grAADrawTriangle@24",
	"_grAlphaBlendFunction@16",
	"_grAlphaCombine@20",
	"_grAlphaControlsITRGBLighting@4",
	"_grAlphaTestFunction@4",
	"_grAlphaTestReferenceValue@4",
	"_grBufferClear@12",
	"_grBufferNumPending@0",
	"_grBufferSwap@4",
	"_grCheckForRoom@4",
	"_grChromakeyMode@4",
	"_grChromakeyValue@4",
	"_grClipWindow@16",
	"_grColorCombine@20",
	"_grColorMask@8",
	"_grConstantColorValue4@16",
	"_grConstantColorValue@4",
	"_grCullMode@4",
	"_grDepthBiasLevel@4",
	"_grDepthBufferFunction@4",
	"_grDepthBufferMode@4",
	"_grDepthMask@4",
	"_grDisableAllEffects@0",
	"_grDitherMode@4",
	"_grDrawLine@8",
	"_grDrawPlanarPolygon@12",
	"_grDrawPlanarPolygonVertexList@8",
	"_grDrawPoint@4",
	"_grDrawPolygon@12",
	"_grDrawPolygonVertexList@8",
	"_grDrawTriangle@12",
	"_grErrorSetCallback@4",
	"_grFogColorValue@4",
	"_grFogMode@4",
	"_grFogTable@4",
	"_grGammaCorrectionValue@4",
	"_grGlideGetState@4",
	"_grGlideGetVersion@4",
	"_grGlideInit@0",
	"_grGlideSetState@4",
	"_grGlideShamelessPlug@4",
	"_grGlideShutdown@0",
	"_grHints@8",
	"_grLfbConstantAlpha@4",
	"_grLfbConstantDepth@4",
	"_grLfbLock@24",
	"_grLfbReadRegion@28",
	"_grLfbWriteRegion@32",
	"_grLfbUnlock@8",
	"_grLfbWriteColorFormat@4",
	"_grLfbWriteColorSwizzle@8",
	"_grRenderBuffer@4",
	"_grResetTriStats@0",
	"_grSplash@20",
	"_grSstConfigPipeline@12",
	"_grSstControl@4",
	"_grSstIdle@0",
	"_grSstIsBusy@0",
	"_grSstOrigin@4",
	"_grSstPerfStats@4",
	"_grSstQueryBoards@4",
	"_grSstQueryHardware@4",
	"_grSstResetPerfStats@0",
	"_grSstScreenHeight@0",
	"_grSstScreenWidth@0",
	"_grSstSelect@4",
	"_grSstStatus@0",
	"_grSstVideoLine@0",
	"_grSstVidMode@8",
	"_grSstVRetraceOn@0",
	"_grSstWinClose@0",
	"_grSstWinOpen@28",
	"_grTexCalcMemRequired@16",
	"_grTexClampMode@12",
	"_grTexCombine@28",
	"_grTexCombineFunction@8",
	"_grTexDetailControl@16",
	"_grTexDownloadMipMap@16",
	"_grTexDownloadMipMapLevel@32",
	"_grTexDownloadMipMapLevelPartial@40",
	"_grTexDownloadTable@12",
	"_grTexDownloadTablePartial@20",
	"_grTexFilterMode@12",
	"_grTexLodBiasValue@8",
	"_grTexMaxAddress@4",
	"_grTexMinAddress@4",
	"_grTexMipMapMode@12",
	"_grTexMultibase@8",
	"_grTexMultibaseAddress@20",
	"_grTexNCCTable@8",
	"_grTexSource@16",
	"_grTexTextureMemRequired@8",
	"_grTriStats@8",
	"_gu3dfGetInfo@8",
	"_gu3dfLoad@8",
	"_guAADrawTriangleWithClip@12",
	"_guAlphaSource@4",
	"_guColorCombineFunction@4",
	"_guDrawPolygonVertexListWithClip@8",
	"_guDrawTriangleWithClip@12",
	"_guEncodeRLE16@16",
	"_guEndianSwapBytes@4",
	"_guEndianSwapWords@4",
	"_guFogGenerateExp2@8",
	"_guFogGenerateExp@8",
	"_guFogGenerateLinear@12",
	"_guFogTableIndexToW@4",
	"_guMovieSetName@4",
	"_guMovieStart@0",
	"_guMovieStop@0",
	"_guMPDrawTriangle@12",
	"_guMPInit@0",
	"_guMPTexCombineFunction@4",
	"_guMPTexSource@8",
	"_guTexAllocateMemory@60",
	"_guTexChangeAttributes@48",
	"_guTexCombineFunction@8",
	"_guTexCreateColorMipMap@0",
	"_guTexDownloadMipMap@12",
	"_guTexDownloadMipMapLevel@12",
	"_guTexGetCurrentMipMap@4",
	"_guTexGetMipMapInfo@4",
	"_guTexMemQueryAvail@4",
	"_guTexMemReset@0",
	"_guTexSource@4",
	"_ConvertAndDownloadRle@64"
};
