#ifndef QB_CONFIG_H__
#define QB_CONFIG_H__

#define PACKAGE_NAME "retroarch"
#define HAVE_7ZIP 1
/* #undef HAVE_AL */
#if __cplusplus || __STDC_VERSION__ >= 199901L
#define HAVE_ALSA 1
#endif
#define HAVE_BUILTINFLAC 1
/* #undef HAVE_BUILTINMINIUPNPC */
#define HAVE_BUILTINZLIB 1
#define HAVE_C99 1
/* #undef HAVE_CACA */
#define HAVE_CC 1
/* #undef HAVE_CG */
#define HAVE_CHD 1
/* #undef HAVE_CHEEVOS */
#define HAVE_COMMAND 1
#define HAVE_CXX 1
/* #undef HAVE_D3D10 */
/* #undef HAVE_D3D11 */
/* #undef HAVE_D3D12 */
/* #undef HAVE_D3D8 */
/* #undef HAVE_D3D9 */
#define HAVE_D3DX 1
/* #undef HAVE_DBUS */
/* #undef HAVE_DISCORD */
/* #undef HAVE_DISPMANX */
#define HAVE_DYLIB 1
#define HAVE_DYNAMIC 1
/* #undef HAVE_EGL */
/* #undef HAVE_EXYNOS */
/* #undef HAVE_FFMPEG */
/* #undef HAVE_FLAC */
#define HAVE_FLOATHARD 1
/* #undef HAVE_FLOATSOFTFP */
/* #undef HAVE_FREETYPE */
/* HAVE_GETOPT_LONG disabled: use RetroArch built-in getopt */
/* #undef HAVE_HID */
/* #undef HAVE_HLSL */
#define HAVE_IBXM 1
/* #undef HAVE_IMAGEVIEWER */
/* #undef HAVE_JACK */
/* #undef HAVE_KMS */
#define HAVE_LANGEXTRA 1
#define HAVE_LIBRETRODB 1
/* #undef HAVE_LIBUSB */
/* #undef HAVE_LIBXML2 */
/* #undef HAVE_MALI_FBDEV */
/* #undef HAVE_MATERIALUI */
/* #undef HAVE_MINIUPNPC */
#define HAVE_MMAP 1
/* #undef HAVE_MPV */
#define HAVE_NEON 1
/* #undef HAVE_NETWORKGAMEPAD */
/* #undef HAVE_NETWORKING */
/* #undef HAVE_NETWORK_CMD */
#define HAVE_NOUNUSED 1
#define HAVE_NOUNUSED_VARIABLE 1
#define HAVE_NO_AL 1
#define HAVE_NO_BUILTINMINIUPNPC 1
#define HAVE_NO_CACA 1
#define HAVE_NO_CG 1
#define HAVE_NO_CHEEVOS 1
#define HAVE_NO_DBUS 1
#define HAVE_NO_DISCORD 1
#define HAVE_NO_EGL 1
#define HAVE_NO_FFMPEG 1
#define HAVE_NO_FREETYPE 1
#define HAVE_NO_HID 1
#define HAVE_NO_IMAGEVIEWER 1
#define HAVE_NO_JACK 1
#define HAVE_NO_KMS 1
#define HAVE_NO_LIBUSB 1
#define HAVE_NO_LIBXML2 1
#define HAVE_NO_MATERIALUI 1
#define HAVE_NO_MINIUPNPC 1
#define HAVE_NO_MPV 1
#define HAVE_NO_NETWORKING 1
#define HAVE_NO_OPENGL 1
#define HAVE_NO_OSS 1
#define HAVE_NO_PARPORT 1
#define HAVE_NO_QT 1
#define HAVE_NO_ROAR 1
#define HAVE_NO_RSOUND 1
#define HAVE_NO_SDL 1
#define HAVE_NO_SHADERPIPELINE 1
#define HAVE_NO_SIXEL 1
#define HAVE_NO_SSA 1
#define HAVE_NO_SYSTEMD 1
#define HAVE_NO_TINYALSA 1
#define HAVE_NO_UPDATE_ASSETS 1
#define HAVE_NO_V4L2 1
#define HAVE_NO_VG 1
#define HAVE_NO_VIDEOCORE 1
#define HAVE_NO_VIDEOPROCESSOR 1
#define HAVE_NO_VULKAN 1
#define HAVE_NO_WAYLAND 1
#define HAVE_NO_X11 1
#define HAVE_NO_XMB 1
/* #undef HAVE_NUKLEAR */
/* #undef HAVE_OMAP */
/* #undef HAVE_OPENDINGUX_FBDEV */
/* #undef HAVE_OPENGL */
/* #undef HAVE_OPENGLES */
/* #undef HAVE_OPENGLES3 */
/* #undef HAVE_OSMESA */
/* #undef HAVE_OSS */
#define HAVE_OVERLAY 1
/* #undef HAVE_PARPORT */
/* #undef HAVE_PLAIN_DRM */
/* #undef HAVE_PRESERVE_DYLIB */
#if __cplusplus || __STDC_VERSION__ >= 199901L
#define HAVE_PULSE 1
#endif
/* #undef HAVE_PYTHON */
/* #undef HAVE_QT */
#define HAVE_RBMP 1
#define HAVE_RGUI 1
#define HAVE_RJPEG 1
/* #undef HAVE_ROAR */
#define HAVE_RPILED 1
#define HAVE_RPNG 1
/* #undef HAVE_RSOUND */
#define HAVE_RTGA 1
#define HAVE_RUNAHEAD 1
/* #undef HAVE_SDL */
#if __cplusplus || __STDC_VERSION__ >= 199901L
#define HAVE_SDL2 1
#endif
/* #undef HAVE_SHADERPIPELINE */
/* #undef HAVE_SIXEL */
/* #undef HAVE_SSA */
/* #undef HAVE_SSE */
#if __cplusplus || __STDC_VERSION__ >= 199901L
#define HAVE_SSL 1
#endif
#define HAVE_STB_FONT 1
#define HAVE_STB_IMAGE 1
#define HAVE_STB_VORBIS 1
#define HAVE_STDIN_CMD 1
#define HAVE_STRCASESTR 1
/* #undef HAVE_STRIPES */
/* #undef HAVE_SUNXI */
/* #undef HAVE_SYSTEMD */
#define HAVE_THREADS 1
#define HAVE_THREAD_STORAGE 1
#define HAVE_TINYALSA 1
#define HAVE_UDEV 1
/* #undef HAVE_UPDATE_ASSETS */
/* #undef HAVE_V4L2 */
/* #undef HAVE_VG */
/* #undef HAVE_VIDEOCORE */
/* #undef HAVE_VIDEOPROCESSOR */
/* #undef HAVE_VIVANTE_FBDEV */
/* #undef HAVE_VULKAN */
#define HAVE_VULKAN_DISPLAY 1
/* #undef HAVE_WAYLAND */
/* #undef HAVE_WAYLAND_CURSOR */
/* #undef HAVE_X11 */
/* #undef HAVE_XCB */
/* #undef HAVE_XEXT */
/* #undef HAVE_XF86VM */
/* #undef HAVE_XINERAMA */
/* #undef HAVE_XKBCOMMON */
/* #undef HAVE_XMB */
/* #undef HAVE_XSHM */
/* #undef HAVE_XVIDEO */
/* #undef HAVE_ZARCH */
#define HAVE_ZLIB 1
#endif
