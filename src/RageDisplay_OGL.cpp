#include "global.h"

/* ours may be more up-to-date */
#define __glext_h_

#if defined(WIN32)
#include <windows.h>
#endif

#if !defined(DARWIN)
# include <GL/gl.h>
# include <GL/glu.h>
#else
# include <OpenGL/gl.h>
# include <OpenGL/glu.h>
#endif

#undef __glext_h_
#include "glext.h"

#include "RageFile.h"
#include "RageSurface.h"
#include "RageSurfaceUtils.h"
#include "PrefsManager.h" // XXX

/* Windows's broken gl.h defines GL_EXT_paletted_texture incompletely: */
#ifndef GL_TEXTURE_INDEX_SIZE_EXT
#define GL_TEXTURE_INDEX_SIZE_EXT         0x80ED
#endif
#include <set>
#include <sstream>

/* Not in glext.h: */
typedef bool (APIENTRY * PWSWAPINTERVALEXTPROC) (int interval);
typedef int ( *PFNGLXSWAPINTERVALSGIPROC) (int interval);

/* Extension functions we use.  Put these in a namespace instead of in oglspecs_t,
 * so they can be called like regular functions. */
static struct GLExt_t
{
	PWSWAPINTERVALEXTPROC wglSwapIntervalEXT;
	PFNGLXSWAPINTERVALSGIPROC glXSwapIntervalSGI;
	PFNGLCOLORTABLEPROC glColorTableEXT;
	PFNGLCOLORTABLEPARAMETERIVPROC glGetColorTableParameterivEXT;
	PFNGLACTIVETEXTUREARBPROC glActiveTextureARB;
	PFNGLGENBUFFERSARBPROC glGenBuffersARB;
	PFNGLBINDBUFFERARBPROC glBindBufferARB;
	PFNGLBUFFERDATAARBPROC glBufferDataARB;
	PFNGLBUFFERSUBDATAARBPROC glBufferSubDataARB;
	PFNGLDELETEBUFFERSARBPROC glDeleteBuffersARB;
	PFNGLDRAWRANGEELEMENTSPROC glDrawRangeElements;
	
	void Reset()
	{
		memset( this, 0, sizeof(*this) );
	}
} GLExt;

#if defined(DARWIN)
#include "archutils/Darwin/Vsync.h"
#endif

#include "RageDisplay.h"
#include "RageDisplay_OGL.h"
#include "RageUtil.h"
#include "RageLog.h"
#include "RageTimer.h"
#include "RageException.h"
#include "RageTexture.h"
#include "RageTextureManager.h"
#include "RageMath.h"
#include "RageTypes.h"
#include "GameConstantsAndTypes.h"
#include "StepMania.h"
#include "RageUtil.h"

#include "arch/arch.h"
#include "arch/LowLevelWindow/LowLevelWindow.h"

#ifdef WIN32
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "glu32.lib")
#endif

//
// Globals
//

static bool g_bEXT_texture_env_combine = true;
static bool g_bGL_EXT_bgra = true;

static bool g_bReversePackedPixelsWorks = true;

/* OpenGL system information that generally doesn't change at runtime. */

/* Range and granularity of points and lines: */
float g_line_range[2];
float g_line_granularity;
float g_point_range[2];
float g_point_granularity;

/* OpenGL version * 10: */
int g_glVersion;

/* GLU version * 10: */
int g_gluVersion;

/* Available extensions: */
set<string> g_glExts;

/* We don't actually use normals (we don't tunr on lighting), there's just
 * no GL_T2F_C4F_V3F. */
const GLenum RageSpriteVertexFormat = GL_T2F_C4F_N3F_V3F;


LowLevelWindow *wind;

static void InvalidateAllGeometry();

static RageDisplay::PixelFormatDesc PIXEL_FORMAT_DESC[RageDisplay::NUM_PIX_FORMATS] = {
	{
		/* R8G8B8A8 */
		32,
		{ 0xFF000000,
		  0x00FF0000,
		  0x0000FF00,
		  0x000000FF }
	}, {
		/* R4G4B4A4 */
		16,
		{ 0xF000,
		  0x0F00,
		  0x00F0,
		  0x000F },
	}, {
		/* R5G5B5A1 */
		16,
		{ 0xF800,
		  0x07C0,
		  0x003E,
		  0x0001 },
	}, {
		/* R5G5B5 */
		16,
		{ 0xF800,
		  0x07C0,
		  0x003E,
		  0x0000 },
	}, {
		/* R8G8B8 */
		24,
		{ 0xFF0000,
		  0x00FF00,
		  0x0000FF,
		  0x000000 }
	}, {
		/* Paletted */
		8,
		{ 0,0,0,0 } /* N/A */
	}, {
		/* B8G8R8A8 */
		24,
		{ 0x0000FF,
		  0x00FF00,
		  0xFF0000,
		  0x000000 }
	}, {
		/* A1B5G5R5 */
		16,
		{ 0x7C00,
		  0x03E0,
		  0x001F,
		  0x8000 },
	}
};

static map<GLenum, CString> g_Strings;
static void InitStringMap()
{
	static bool Initialized = false;
	if(Initialized) return;
	Initialized = true;
	#define X(a) g_Strings[a] = #a;
	X(GL_RGBA8);	X(GL_RGBA4);	X(GL_RGB5_A1);	X(GL_RGB5);	X(GL_RGBA);	X(GL_RGB);
	X(GL_BGR);	X(GL_BGRA);
	X(GL_COLOR_INDEX8_EXT);	X(GL_COLOR_INDEX4_EXT);	X(GL_COLOR_INDEX);
	X(GL_UNSIGNED_BYTE);	X(GL_UNSIGNED_SHORT_4_4_4_4); X(GL_UNSIGNED_SHORT_5_5_5_1);
	X(GL_UNSIGNED_SHORT_1_5_5_5_REV);
	X(GL_INVALID_ENUM); X(GL_INVALID_VALUE); X(GL_INVALID_OPERATION);
	X(GL_STACK_OVERFLOW); X(GL_STACK_UNDERFLOW); X(GL_OUT_OF_MEMORY);
}

static CString GLToString( GLenum e )
{
	if( g_Strings.find(e) != g_Strings.end() )
		return g_Strings[e];

	return ssprintf( "%i", int(e) );
}

/* GL_PIXFMT_INFO is used for both texture formats and surface formats.  For example,
 * it's fine to ask for a FMT_RGB5 texture, but to supply a surface matching
 * FMT_RGB8.  OpenGL will simply discard the extra bits.
 *
 * It's possible for a format to be supported as a texture format but not as a
 * surface format.  For example, if packed pixels aren't supported, we can still
 * use GL_RGB5_A1, but we'll have to convert to a supported surface pixel format
 * first.  It's not ideal, since we'll convert to RGBA8 and OGL will convert back,
 * but it works fine.
 */
struct GLPixFmtInfo_t {
	GLenum internalfmt; /* target format */
	GLenum format; /* target format */
	GLenum type; /* data format */
} GL_PIXFMT_INFO[RageDisplay::NUM_PIX_FORMATS] = {
	{
		/* R8G8B8A8 */
		GL_RGBA8,
		GL_RGBA,
		GL_UNSIGNED_BYTE,
	}, {
		/* B4G4R4A4 */
		GL_RGBA4,
		GL_RGBA,
		GL_UNSIGNED_SHORT_4_4_4_4,
	}, {
		/* B5G5R5A1 */
		GL_RGB5_A1,
		GL_RGBA,
		GL_UNSIGNED_SHORT_5_5_5_1,
	}, {
		/* B5G5R5 */
		GL_RGB5,
		GL_RGBA,
		GL_UNSIGNED_SHORT_5_5_5_1,
	}, {
		/* B8G8R8 */
		GL_RGB8,
		GL_RGB,
		GL_UNSIGNED_BYTE,
	}, {
		/* Paletted */
		GL_COLOR_INDEX8_EXT,
		GL_COLOR_INDEX,
		GL_UNSIGNED_BYTE,
	}, {
		/* B8G8R8 */
		GL_RGB8,
		GL_BGR,
		GL_UNSIGNED_BYTE,
	}, {
		/* A1R5G5B5 (matches D3DFMT_A1R5G5B5) */
		GL_RGB5_A1,
		GL_BGRA,
		GL_UNSIGNED_SHORT_1_5_5_5_REV,
	}
};


static void FixLilEndian()
{
#if defined(ENDIAN_LITTLE)
	static bool Initialized = false;
	if( Initialized )
		return;
	Initialized = true;

	for( int i = 0; i < RageDisplay::NUM_PIX_FORMATS; ++i )
	{
		RageDisplay::PixelFormatDesc &pf = PIXEL_FORMAT_DESC[i];

		/* OpenGL and RageSurface handle byte formats differently; we need
		 * to flip non-paletted masks to make them line up. */
		if( GL_PIXFMT_INFO[i].type != GL_UNSIGNED_BYTE || pf.bpp == 8 )
			continue;

		for( int mask = 0; mask < 4; ++mask)
		{
			int m = pf.masks[mask];
			switch( pf.bpp )
			{
			case 24: m = Swap24(m); break;
			case 32: m = Swap32(m); break;
			default: ASSERT(0);
			}
			pf.masks[mask] = m;
		}
	}
#endif
}


void GetGLExtensions(set<string> &ext)
{
    const char *buf = (const char *)glGetString(GL_EXTENSIONS);

	vector<CString> lst;
	split(buf, " ", lst);

	for(unsigned i = 0; i < lst.size(); ++i)
		ext.insert(lst[i]);
}

static void FlushGLErrors()
{
	/* Making an OpenGL call doesn't also flush the error state; if we happen
	 * to have an error from a previous call, then the assert below will fail. 
	 * Flush it. */
	while( glGetError() != GL_NO_ERROR )
		;
}

#define AssertNoGLError() \
{ \
	GLenum error = glGetError(); \
	ASSERT_M( error == GL_NO_ERROR, GLToString(error) ) \
}

static void TurnOffHardwareVBO()
{
	if( GLExt.glBindBufferARB )
	{
		GLExt.glBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
		GLExt.glBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, 0 );
	}
}

#if defined(UNIX)
#define Font X11___Font
#define Screen X11___Screen
#include "GL/glx.h"
#undef Font
#undef Screen

Display *g_X11Display = NULL;
#endif

static void LogGLXDebugInformation()
{
#if 0
	ASSERT( g_X11Display );

	const int scr = DefaultScreen( g_X11Display );

	LOG->Info( "Display: %s (screen %i)", DisplayString(g_X11Display), scr );
	LOG->Info( "Direct rendering: %s", glXIsDirect( g_X11Display, glXGetCurrentContext() )? "yes":"no" );

	int XServerVersion = XVendorRelease( g_X11Display ); /* eg. 40201001 */
	int major = XServerVersion / 10000000; XServerVersion %= 10000000;
	int minor = XServerVersion / 100000;   XServerVersion %= 100000;
	int revision = XServerVersion / 1000;  XServerVersion %= 1000;
	int patch = XServerVersion;

	LOG->Info( "X server vendor: %s [%i.%i.%i.%i]", XServerVendor( g_X11Display ), major, minor, revision, patch );
	LOG->Info( "Server GLX vendor: %s [%s]", glXQueryServerString( g_X11Display, scr, GLX_VENDOR ), glXQueryServerString( g_X11Display, scr, GLX_VERSION ) );
	LOG->Info( "Client GLX vendor: %s [%s]", glXGetClientString( g_X11Display, GLX_VENDOR ), glXGetClientString( g_X11Display, GLX_VERSION ) );
#endif
}

RageDisplay_OGL::RageDisplay_OGL( VideoModeParams p, bool bAllowUnacceleratedRenderer )
{
	LOG->Trace( "RageDisplay_OGL::RageDisplay_OGL()" );
	LOG->MapLog("renderer", "Current renderer: OpenGL");

	FixLilEndian();
	InitStringMap();

	wind = MakeLowLevelWindow();

	try {
		SetVideoMode( p );
	} catch(...) {
		/* SetVideoMode can throw. */
		delete wind;
		throw;
	}

	// Log driver details
	LOG->Info("OGL Vendor: %s", glGetString(GL_VENDOR));
	LOG->Info("OGL Renderer: %s", glGetString(GL_RENDERER));
	LOG->Info("OGL Version: %s", glGetString(GL_VERSION));
	LOG->Info("OGL Extensions: %s", glGetString(GL_EXTENSIONS));
	LOG->Info("OGL Max texture size: %i", GetMaxTextureSize() );
	LOG->Info("GLU Version: %s", gluGetString(GLU_VERSION));

	LogGLXDebugInformation();

	if( IsSoftwareRenderer() )
	{
		if( !bAllowUnacceleratedRenderer )
		{
			delete wind;
			RageException::ThrowNonfatal(
				"Your system is reporting that OpenGL hardware acceleration is not available.  "
				"Please obtain an updated driver from your video card manufacturer.\n\n" );
		}
		LOG->Warn("This is a software renderer!");
	}

#if defined(_WINDOWS)
	/* GLDirect is a Direct3D wrapper for OpenGL.  It's rather buggy; and if in
	 * any case GLDirect can successfully render us, we should be able to do so
	 * too using Direct3D directly.  (If we can't, it's a bug that we can work
	 * around--if GLDirect can do it, so can we!) */
	if( !strncmp( (const char *) glGetString(GL_RENDERER), "GLDirect", 8 ) )
	{
		delete wind;
		RageException::ThrowNonfatal( "GLDirect was detected.  GLDirect is not compatible with StepMania, and should be disabled.\n" );
	}
#endif

#if defined(UNIX)
	if( !glXIsDirect( g_X11Display, glXGetCurrentContext() ) )
	{
		if( !bAllowUnacceleratedRenderer )
		{
			delete wind;
			RageException::ThrowNonfatal(
				"Your system is reporting that direct rendering is not available.  "
				"Please obtain an updated driver from your video card manufacturer." );
		}
		LOG->Warn("Direct rendering is not enabled!");
	}
#endif

	/* Log this, so if people complain that the radar looks bad on their
	 * system we can compare them: */
	glGetFloatv(GL_LINE_WIDTH_RANGE, g_line_range);
	glGetFloatv(GL_LINE_WIDTH_GRANULARITY, &g_line_granularity);
	glGetFloatv(GL_POINT_SIZE_RANGE, g_point_range);
	glGetFloatv(GL_POINT_SIZE_GRANULARITY, &g_point_granularity);
}

#if defined(UNIX) && defined(HAVE_LIBXTST)
#include <X11/extensions/XTest.h>
#endif

void RageDisplay_OGL::Update(float fDeltaTime)
{
	wind->Update(fDeltaTime);

	if( PREFSMAN->m_bDisableScreenSaver )
	{
		/* Disable the screensaver. */
#if defined(UNIX) && defined(HAVE_LIBXTST)
		ASSERT( g_X11Display );

		/* This causes flicker. */
		// XForceScreenSaver( g_X11Display, ScreenSaverReset );
		
		/*
		 * Instead, send a null relative mouse motion, to trick X into thinking there has been
		 * user activity. 
		 *
		 * This also handles XScreenSaver; XForceScreenSaver only handles the internal X11
		 * screen blanker.
		 *
		 * This will delay the X blanker, DPMS and XScreenSaver from activating, and will
		 * disable the blanker and XScreenSaver if they're already active (unless XSS is
		 * locked).  For some reason, it doesn't un-blank DPMS if it's already active.
		 */

		XLockDisplay( g_X11Display );

		int event_base, error_base, major, minor;
		if( XTestQueryExtension( g_X11Display, &event_base, &error_base, &major, &minor ) )
		{
			XTestFakeRelativeMotionEvent( g_X11Display, 0, 0, 0 );
			XSync( g_X11Display, False );
		}

		XUnlockDisplay( g_X11Display );
#endif
	}
}

bool RageDisplay_OGL::IsSoftwareRenderer()
{
#if defined(WIN32)
	return 
		( strcmp((const char*)glGetString(GL_VENDOR),"Microsoft Corporation")==0 ) &&
		( strcmp((const char*)glGetString(GL_RENDERER),"GDI Generic")==0 );
#else
	return false;
#endif
}

RageDisplay_OGL::~RageDisplay_OGL()
{
	delete wind;
}

bool HasExtension(CString ext)
{
	return g_glExts.find(ext) != g_glExts.end();
}

static void CheckPalettedTextures()
{
	CString error;
	do
	{
		if( !HasExtension("GL_EXT_paletted_texture") )
		{
			error = "GL_EXT_paletted_texture missing";
			break;
		}

		if( GLExt.glColorTableEXT == NULL )
		{
			error = "glColorTableEXT missing";
			break;
		}

		if( GLExt.glGetColorTableParameterivEXT == NULL )
		{
			error = "glGetColorTableParameterivEXT missing";
			break;
		}

		/* Check to see if paletted textures really work. */
		GLenum glTexFormat = GL_PIXFMT_INFO[RageDisplay::FMT_PAL].internalfmt;
		GLenum glImageFormat = GL_PIXFMT_INFO[RageDisplay::FMT_PAL].format;
		GLenum glImageType = GL_PIXFMT_INFO[RageDisplay::FMT_PAL].type;

		int bits = 8;

		FlushGLErrors();
#define GL_CHECK_ERROR(f) \
{ \
	GLenum glError = glGetError(); \
	if( glError != GL_NO_ERROR ) { \
		error = ssprintf(f " failed (%s)", GLToString(glError).c_str() ); \
		break; \
	} \
}

		glTexImage2D(GL_PROXY_TEXTURE_2D,
				0, glTexFormat, 
				16, 16, 0,
				glImageFormat, glImageType, NULL);
		GL_CHECK_ERROR( "glTexImage2D" );

		GLuint ifmt = 0;
		glGetTexLevelParameteriv( GL_PROXY_TEXTURE_2D, 0, GLenum(GL_TEXTURE_INTERNAL_FORMAT), (GLint *)&ifmt );
		GL_CHECK_ERROR( "glGetTexLevelParameteriv(GL_TEXTURE_INTERNAL_FORMAT)" );
		if( ifmt != glTexFormat )
		{
			error = ssprintf( "Expected format %s, got %s instead",
					GLToString(glTexFormat).c_str(),
					GLToString(ifmt).c_str() );
			break;
		}

		GLubyte palette[256*4];
		memset(palette, 0, sizeof(palette));
		GLExt.glColorTableEXT(GL_PROXY_TEXTURE_2D, GL_RGBA8, 256, GL_RGBA, GL_UNSIGNED_BYTE, palette);
		GL_CHECK_ERROR( "glColorTableEXT" );

		GLint size = 0;
		glGetTexLevelParameteriv(GL_PROXY_TEXTURE_2D, 0, GLenum(GL_TEXTURE_INDEX_SIZE_EXT), &size);
		GL_CHECK_ERROR( "glGetTexLevelParameteriv(GL_TEXTURE_INDEX_SIZE_EXT)" );
		if( bits > size || size > 8 )
		{
			error = ssprintf("Expected %i-bit palette, got a %i-bit one instead", bits, int(size));
			break;
		}

		GLint RealWidth = 0;
		GLExt.glGetColorTableParameterivEXT(GL_PROXY_TEXTURE_2D, GL_COLOR_TABLE_WIDTH, &RealWidth);
		GL_CHECK_ERROR( "glGetColorTableParameterivEXT(GL_COLOR_TABLE_WIDTH)" );
		if( RealWidth != 1 << bits )
		{
			error = ssprintf("GL_COLOR_TABLE_WIDTH returned %i instead of %i", int(RealWidth), 1 << bits );
			break;
		}

		GLint RealFormat = 0;
		GLExt.glGetColorTableParameterivEXT(GL_PROXY_TEXTURE_2D, GL_COLOR_TABLE_FORMAT, &RealFormat);
		GL_CHECK_ERROR( "glGetColorTableParameterivEXT(GL_COLOR_TABLE_FORMAT)" );
		if( RealFormat != GL_RGBA8 )
		{
			error = ssprintf("GL_COLOR_TABLE_FORMAT returned %s instead of GL_RGBA8", GLToString(RealFormat).c_str() );
			break;
		}
	} while(0);
#undef GL_CHECK_ERROR

	if( error == "" )
		return;

	/* If 8-bit palettes don't work, disable them entirely--don't trust 4-bit
	 * palettes if it can't even get 8-bit ones right. */
	GLExt.glColorTableEXT = NULL;
	GLExt.glGetColorTableParameterivEXT = NULL;
	LOG->Info("Paletted textures disabled: %s.", error.c_str());
}

static void CheckReversePackedPixels()
{
	/* Try to create a texture. */
	FlushGLErrors();
	glTexImage2D(GL_PROXY_TEXTURE_2D,
				0, GL_RGBA, 
				16, 16, 0,
				GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV, NULL);

	const GLenum glError = glGetError();
	if( glError == GL_NO_ERROR )
		g_bReversePackedPixelsWorks = true;
	else
	{
		g_bReversePackedPixelsWorks = false;
		LOG->Info("GL_UNSIGNED_SHORT_1_5_5_5_REV failed (%s), disabled",
			GLToString(glError).c_str() );
	}
}

void SetupExtensions()
{
	const float fGLVersion = strtof( (const char *) glGetString(GL_VERSION), NULL );
	g_glVersion = int(roundf(fGLVersion * 10));
	GetGLExtensions(g_glExts);

	const float fGLUVersion = strtof( (const char *) gluGetString(GLU_VERSION), NULL );
	g_gluVersion = int(roundf(fGLUVersion * 10));

	/* Reset extensions. */
	GLExt.Reset();
	
	/*
	 * Find extension functions.
	 *
	 * X11R6.7.0 (or possibly ATI's drivers) seem to be returning bogus values for glBindBufferARB
	 * if we don't actually check for GL_ARB_vertex_buffer_object. 
	 * https://sf.net/tracker/download.php?group_id=37892&atid=421366&file_id=88086&aid=958820
	 * https://sf.net/tracker/download.php?group_id=37892&atid=421366&file_id=85542&aid=944836
	 *
	 * Let's check them all, to be safe.
	 */

#if defined(WIN32)
	if( HasExtension("WGL_EXT_swap_control") )
		GLExt.wglSwapIntervalEXT = (PWSWAPINTERVALEXTPROC) wind->GetProcAddress("wglSwapIntervalEXT");
#elif defined(DARWIN)
	GLExt.wglSwapIntervalEXT = wglSwapIntervalEXT;
#endif

        GLExt.glXSwapIntervalSGI = (PFNGLXSWAPINTERVALSGIPROC) wind->GetProcAddress("glXSwapIntervalSGI");

	if( HasExtension("GL_EXT_paletted_texture") )
	{
		GLExt.glColorTableEXT = (PFNGLCOLORTABLEPROC) wind->GetProcAddress("glColorTableEXT");
		GLExt.glGetColorTableParameterivEXT = (PFNGLCOLORTABLEPARAMETERIVPROC) wind->GetProcAddress("glGetColorTableParameterivEXT");
	}

	if( HasExtension("GL_ARB_multitexture") )
		GLExt.glActiveTextureARB = (PFNGLACTIVETEXTUREARBPROC) wind->GetProcAddress("glActiveTextureARB");

	if( HasExtension("GL_ARB_vertex_buffer_object") )
	{
		GLExt.glGenBuffersARB = (PFNGLGENBUFFERSARBPROC) wind->GetProcAddress("glGenBuffersARB");
		GLExt.glBindBufferARB = (PFNGLBINDBUFFERARBPROC) wind->GetProcAddress("glBindBufferARB");
		GLExt.glBufferDataARB = (PFNGLBUFFERDATAARBPROC) wind->GetProcAddress("glBufferDataARB");
		GLExt.glBufferSubDataARB = (PFNGLBUFFERSUBDATAARBPROC) wind->GetProcAddress("glBufferSubDataARB");
		GLExt.glDeleteBuffersARB = (PFNGLDELETEBUFFERSARBPROC) wind->GetProcAddress("glDeleteBuffersARB");
	}

	if( HasExtension("GL_EXT_draw_range_elements") )
		GLExt.glDrawRangeElements = (PFNGLDRAWRANGEELEMENTSPROC) wind->GetProcAddress("glDrawRangeElements");

	g_bEXT_texture_env_combine = HasExtension("GL_EXT_texture_env_combine");
	g_bGL_EXT_bgra = HasExtension("GL_EXT_bgra");
	CheckPalettedTextures();
	CheckReversePackedPixels();
}

void DumpOpenGLDebugInfo()
{
#if defined(WIN32)
	/* Dump Windows pixel format data. */
	int Actual = GetPixelFormat(wglGetCurrentDC());

	PIXELFORMATDESCRIPTOR pfd;
	memset(&pfd, 0, sizeof(pfd));
	pfd.nSize=sizeof(pfd);
	pfd.nVersion=1;
  
	int pfcnt = DescribePixelFormat(GetDC(g_hWndMain),1,sizeof(pfd),&pfd);
	for (int i=1; i <= pfcnt; i++)
	{
		memset(&pfd, 0, sizeof(pfd));
		pfd.nSize=sizeof(pfd);
		pfd.nVersion=1;
		DescribePixelFormat(GetDC(g_hWndMain),i,sizeof(pfd),&pfd);

		bool skip = false;

		bool rgba = (pfd.iPixelType==PFD_TYPE_RGBA);

		bool mcd = ((pfd.dwFlags & PFD_GENERIC_FORMAT) && (pfd.dwFlags & PFD_GENERIC_ACCELERATED));
		bool soft = ((pfd.dwFlags & PFD_GENERIC_FORMAT) && !(pfd.dwFlags & PFD_GENERIC_ACCELERATED));
		bool icd = !(pfd.dwFlags & PFD_GENERIC_FORMAT) && !(pfd.dwFlags & PFD_GENERIC_ACCELERATED);
		bool opengl = !!(pfd.dwFlags & PFD_SUPPORT_OPENGL);
		bool window = !!(pfd.dwFlags & PFD_DRAW_TO_WINDOW);
		bool dbuff = !!(pfd.dwFlags & PFD_DOUBLEBUFFER);

		if(!rgba || soft || !opengl || !window || !dbuff)
			skip = true;

		/* Skip the above, unless it happens to be the one we chose. */
		if(skip && i != Actual)
			continue;

		CString str = ssprintf("Mode %i: ", i);
		if( i != Actual )
			continue;
//		if(i == Actual) str += "*** ";
		if(skip) str += "(BOGUS) ";
		if(soft) str += "software ";
		if(icd) str += "ICD ";
		if(mcd) str += "MCD ";
		if(!rgba) str += "indexed ";
		if(!opengl) str += "!OPENGL ";
		if(!window) str += "!window ";
		if(!dbuff) str += "!dbuff ";

		str += ssprintf("%i (%i%i%i) ", pfd.cColorBits, pfd.cRedBits, pfd.cGreenBits, pfd.cBlueBits);
		if(pfd.cAlphaBits) str += ssprintf("%i alpha ", pfd.cAlphaBits);
		if(pfd.cDepthBits) str += ssprintf("%i depth ", pfd.cDepthBits);
		if(pfd.cStencilBits) str += ssprintf("%i stencil ", pfd.cStencilBits);
		if(pfd.cAccumBits) str += ssprintf("%i accum ", pfd.cAccumBits);

		if(i == Actual && skip)
		{
			/* We chose a bogus format. */
			LOG->Warn("%s", str.c_str());
		} else
			LOG->Info("%s", str.c_str());
	}
#endif
}

void RageDisplay_OGL::ResolutionChanged()
{
 	SetViewport(0,0);

	/* Clear any junk that's in the framebuffer. */
	if( BeginFrame() )
		EndFrame();
}

// Return true if mode change was successful.
// bNewDeviceOut is set true if a new device was created and textures
// need to be reloaded.
CString RageDisplay_OGL::TryVideoMode( VideoModeParams p, bool &bNewDeviceOut )
{
//	LOG->Trace( "RageDisplay_OGL::SetVideoMode( %d, %d, %d, %d, %d, %d )", windowed, width, height, bpp, rate, vsync );
	CString err;
	err = wind->TryVideoMode( p, bNewDeviceOut );
	if( err != "" )
		return err;	// failed to set video mode

	if( bNewDeviceOut )
	{
		/* We have a new OpenGL context, so we have to tell our textures that
		 * their OpenGL texture number is invalid. */
		if(TEXTUREMAN)
			TEXTUREMAN->InvalidateTextures();

		/* Recreate all vertex buffers. */
		InvalidateAllGeometry();
	}

	this->SetDefaultRenderStates();
	DumpOpenGLDebugInfo();

	/* Now that we've initialized, we can search for extensions (some of which
	 * we may need to set up the video mode). */
	SetupExtensions();

	/* Set vsync the Windows way, if we can.  (What other extensions are there
	 * to do this, for other archs?) */
	if( GLExt.wglSwapIntervalEXT )
	    GLExt.wglSwapIntervalEXT(p.vsync);
	
	if( GLExt.glXSwapIntervalSGI )
	    GLExt.glXSwapIntervalSGI(p.vsync);
	
	ResolutionChanged();

	return "";	// successfully set mode
}

void RageDisplay_OGL::SetViewport(int shift_left, int shift_down)
{
	/* left and down are on a 0..SCREEN_WIDTH, 0..SCREEN_HEIGHT scale.
	 * Scale them to the actual viewport range. */
	shift_left = int( shift_left * float(wind->GetVideoModeParams().width) / SCREEN_WIDTH );
	shift_down = int( shift_down * float(wind->GetVideoModeParams().height) / SCREEN_HEIGHT );

	glViewport(shift_left, -shift_down, wind->GetVideoModeParams().width, wind->GetVideoModeParams().height);
}

int RageDisplay_OGL::GetMaxTextureSize() const
{
	GLint size;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &size);
	return size;
}

bool RageDisplay_OGL::BeginFrame()
{
	glClearColor( 0,0,0,1 );
	SetZWrite( true );
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	return true;
}

void RageDisplay_OGL::EndFrame()
{
	wind->SwapBuffers();
	ProcessStatsOnFlip();
}

RageSurface* RageDisplay_OGL::CreateScreenshot()
{
	int width = wind->GetVideoModeParams().width;
	int height = wind->GetVideoModeParams().height;

	const PixelFormatDesc &desc = PIXEL_FORMAT_DESC[FMT_RGBA8];
	RageSurface *image = CreateSurface( width, height, desc.bpp,
		desc.masks[0], desc.masks[1], desc.masks[2], 0 );

	FlushGLErrors();
	glReadPixels(0, 0, wind->GetVideoModeParams().width, wind->GetVideoModeParams().height, GL_RGBA,
	             GL_UNSIGNED_BYTE, image->pixels);
	AssertNoGLError();

	RageSurfaceUtils::FlipVertically( image );

	return image;
}

RageDisplay::VideoModeParams RageDisplay_OGL::GetVideoModeParams() const { return wind->GetVideoModeParams(); }

static void SetupVertices( const RageSpriteVertex v[], int iNumVerts )
{
	static float *Vertex, *Texture, *Normal;	
	static GLubyte *Color;
	static int Size = 0;
	if(iNumVerts > Size)
	{
		Size = iNumVerts;
		delete [] Vertex;
		delete [] Color;
		delete [] Texture;
		delete [] Normal;
		Vertex = new float[Size*3];
		Color = new GLubyte[Size*4];
		Texture = new float[Size*2];
		Normal = new float[Size*3];
	}

	for(unsigned i = 0; i < unsigned(iNumVerts); ++i)
	{
		Vertex[i*3+0]  = v[i].p[0];
		Vertex[i*3+1]  = v[i].p[1];
		Vertex[i*3+2]  = v[i].p[2];
		Color[i*4+0]   = v[i].c.r;
		Color[i*4+1]   = v[i].c.g;
		Color[i*4+2]   = v[i].c.b;
		Color[i*4+3]   = v[i].c.a;
		Texture[i*2+0] = v[i].t[0];
		Texture[i*2+1] = v[i].t[1];
		Normal[i*3+0] = v[i].n[0];
		Normal[i*3+1] = v[i].n[1];
		Normal[i*3+2] = v[i].n[2];
	}
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(3, GL_FLOAT, 0, Vertex);

	glEnableClientState(GL_COLOR_ARRAY);
	glColorPointer(4, GL_UNSIGNED_BYTE, 0, Color);

	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer(2, GL_FLOAT, 0, Texture);

	glEnableClientState(GL_NORMAL_ARRAY);
	glNormalPointer(GL_FLOAT, 0, Normal);
}

void RageDisplay_OGL::SendCurrentMatrices()
{
	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( (const float*)GetProjectionTop() );
	RageMatrix modelView;
	RageMatrixMultiply( &modelView, GetCentering(), GetViewTop() );
	RageMatrixMultiply( &modelView, &modelView, GetWorldTop() );
	glMatrixMode( GL_MODELVIEW );
	glLoadMatrixf( (const float*)&modelView );
	glMatrixMode( GL_TEXTURE  );
	glLoadMatrixf( (const float*)GetTextureTop() );
}

class RageCompiledGeometrySWOGL : public RageCompiledGeometry
{
public:
	
	void Allocate( const vector<msMesh> &vMeshes )
	{
		m_vPosition.resize( GetTotalVertices() );
		m_vTexture.resize( GetTotalVertices() );
		m_vNormal.resize( GetTotalVertices() );
		m_vTriangles.resize( GetTotalTriangles() );
	}
	void Change( const vector<msMesh> &vMeshes )
	{
		for( unsigned i=0; i<vMeshes.size(); i++ )
		{
			const MeshInfo& meshInfo = m_vMeshInfo[i];
			const msMesh& mesh = vMeshes[i];
			const vector<RageModelVertex> &Vertices = mesh.Vertices;
			const vector<msTriangle> &Triangles = mesh.Triangles;

			for( unsigned j=0; j<Vertices.size(); j++ )
			{
				m_vPosition[meshInfo.iVertexStart+j] = Vertices[j].p;
				m_vTexture[meshInfo.iVertexStart+j] = Vertices[j].t;
				m_vNormal[meshInfo.iVertexStart+j] = Vertices[j].n;
			}

			for( unsigned j=0; j<Triangles.size(); j++ )
				for( unsigned k=0; k<3; k++ )
				{
					int iVertexIndexInVBO = meshInfo.iVertexStart + Triangles[j].nVertexIndices[k];
					m_vTriangles[meshInfo.iTriangleStart+j].nVertexIndices[k] = (uint16_t) iVertexIndexInVBO;
				}
		}
	}
	void Draw( int iMeshIndex ) const
	{
		TurnOffHardwareVBO();

		const MeshInfo& meshInfo = m_vMeshInfo[iMeshIndex];

		glEnableClientState(GL_VERTEX_ARRAY);
		glVertexPointer(3, GL_FLOAT, 0, &m_vPosition[0]);

		glDisableClientState(GL_COLOR_ARRAY);

		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer(2, GL_FLOAT, 0, &m_vTexture[0]);

		glEnableClientState(GL_NORMAL_ARRAY);
		glNormalPointer(GL_FLOAT, 0, &m_vNormal[0]);

		glDrawElements( 
			GL_TRIANGLES, 
			meshInfo.iTriangleCount*3, 
			GL_UNSIGNED_SHORT, 
			&m_vTriangles[0]+meshInfo.iTriangleStart );
	}

protected:
	vector<RageVector3> m_vPosition;
	vector<RageVector2> m_vTexture;
	vector<RageVector3> m_vNormal;
	vector<msTriangle>	m_vTriangles;
};

class RageCompiledGeometryHWOGL : public RageCompiledGeometrySWOGL
{
protected:
	// vertex buffer object names
	GLuint m_nPositions;
	GLuint m_nTextureCoords;
	GLuint m_nNormals;
	GLuint m_nTriangles;

	void AllocateBuffers();
	void UploadData();

public:
	RageCompiledGeometryHWOGL();
	~RageCompiledGeometryHWOGL();

	/* This is called when our OpenGL context is invalidated. */
	void Invalidate();
	
	void Allocate( const vector<msMesh> &vMeshes );
	void Change( const vector<msMesh> &vMeshes );
	void Draw( int iMeshIndex ) const;
};

static set<RageCompiledGeometryHWOGL *> g_GeometryList;
static void InvalidateAllGeometry()
{
	set<RageCompiledGeometryHWOGL *>::iterator it;
	for( it = g_GeometryList.begin(); it != g_GeometryList.end(); ++it )
		(*it)->Invalidate();
}

RageCompiledGeometryHWOGL::RageCompiledGeometryHWOGL()
{
	g_GeometryList.insert( this );
	m_nPositions = m_nTextureCoords = m_nNormals = m_nTriangles = 0;

	AllocateBuffers();
}

RageCompiledGeometryHWOGL::~RageCompiledGeometryHWOGL()
{
	g_GeometryList.erase( this );
	FlushGLErrors();

	GLExt.glDeleteBuffersARB( 1, &m_nPositions );
	AssertNoGLError();
	GLExt.glDeleteBuffersARB( 1, &m_nTextureCoords );
	AssertNoGLError();
	GLExt.glDeleteBuffersARB( 1, &m_nNormals );
	AssertNoGLError();
	GLExt.glDeleteBuffersARB( 1, &m_nTriangles );
	AssertNoGLError();
}

void RageCompiledGeometryHWOGL::AllocateBuffers()
{
	FlushGLErrors();

	if( !m_nPositions )
	{
		GLExt.glGenBuffersARB( 1, &m_nPositions );
		AssertNoGLError();
	}

	if( !m_nTextureCoords )
	{
		GLExt.glGenBuffersARB( 1, &m_nTextureCoords );
		AssertNoGLError();
	}

	if( !m_nNormals )
	{
		GLExt.glGenBuffersARB( 1, &m_nNormals );
		AssertNoGLError();
	}

	if( !m_nTriangles )
	{
		GLExt.glGenBuffersARB( 1, &m_nTriangles );
		AssertNoGLError();
	}
}

void RageCompiledGeometryHWOGL::UploadData()
{
	GLExt.glBindBufferARB( GL_ARRAY_BUFFER_ARB, m_nPositions );
	GLExt.glBufferDataARB( 
		GL_ARRAY_BUFFER_ARB, 
		GetTotalVertices()*sizeof(RageVector3), 
		&m_vPosition[0], 
		GL_STATIC_DRAW_ARB );
//		AssertNoGLError();

	GLExt.glBindBufferARB( GL_ARRAY_BUFFER_ARB, m_nTextureCoords );
	GLExt.glBufferDataARB( 
		GL_ARRAY_BUFFER_ARB, 
		GetTotalVertices()*sizeof(RageVector2), 
		&m_vTexture[0], 
		GL_STATIC_DRAW_ARB );
//		AssertNoGLError();

	GLExt.glBindBufferARB( GL_ARRAY_BUFFER_ARB, m_nNormals );
	GLExt.glBufferDataARB( 
		GL_ARRAY_BUFFER_ARB, 
		GetTotalVertices()*sizeof(RageVector3), 
		&m_vNormal[0], 
		GL_STATIC_DRAW_ARB );
//		AssertNoGLError();

	GLExt.glBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, m_nTriangles );
	GLExt.glBufferDataARB( 
		GL_ELEMENT_ARRAY_BUFFER_ARB, 
		GetTotalTriangles()*sizeof(msTriangle), 
		&m_vTriangles[0], 
		GL_STATIC_DRAW_ARB );
//		AssertNoGLError();
}

void RageCompiledGeometryHWOGL::Invalidate()
{
	/* Our vertex buffers no longer exist.  Reallocate and reupload. */
	m_nPositions = m_nTextureCoords = m_nNormals = m_nTriangles = 0;
	AllocateBuffers();
	UploadData();
}

void RageCompiledGeometryHWOGL::Allocate( const vector<msMesh> &vMeshes )
{
	RageCompiledGeometrySWOGL::Allocate( vMeshes );
	
	GLExt.glBindBufferARB( GL_ARRAY_BUFFER_ARB, m_nPositions );
	GLExt.glBufferDataARB( 
		GL_ARRAY_BUFFER_ARB, 
		GetTotalVertices()*sizeof(RageVector3), 
		NULL, 
		GL_STATIC_DRAW_ARB );
	AssertNoGLError();

	GLExt.glBindBufferARB( GL_ARRAY_BUFFER_ARB, m_nTextureCoords );
	GLExt.glBufferDataARB( 
		GL_ARRAY_BUFFER_ARB, 
		GetTotalVertices()*sizeof(RageVector2), 
		NULL, 
		GL_STATIC_DRAW_ARB );
	AssertNoGLError();

	GLExt.glBindBufferARB( GL_ARRAY_BUFFER_ARB, m_nNormals );
	GLExt.glBufferDataARB( 
		GL_ARRAY_BUFFER_ARB, 
		GetTotalVertices()*sizeof(RageVector3), 
		NULL, 
		GL_STATIC_DRAW_ARB );
	AssertNoGLError();

	GLExt.glBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, m_nTriangles );
	GLExt.glBufferDataARB( 
		GL_ELEMENT_ARRAY_BUFFER_ARB, 
		GetTotalTriangles()*sizeof(msTriangle), 
		NULL, 
		GL_STATIC_DRAW_ARB );
	AssertNoGLError();
}

void RageCompiledGeometryHWOGL::Change( const vector<msMesh> &vMeshes )
{
	RageCompiledGeometrySWOGL::Change( vMeshes );

	UploadData();
}

void RageCompiledGeometryHWOGL::Draw( int iMeshIndex ) const
{
	FlushGLErrors();

	const MeshInfo& meshInfo = m_vMeshInfo[iMeshIndex];
	if( !meshInfo.iVertexCount || !meshInfo.iTriangleCount )
		return;

	glEnableClientState(GL_VERTEX_ARRAY);
	GLExt.glBindBufferARB( GL_ARRAY_BUFFER_ARB, m_nPositions );
	AssertNoGLError();
	glVertexPointer(3, GL_FLOAT, 0, NULL );
	AssertNoGLError();

	glDisableClientState(GL_COLOR_ARRAY);

	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	GLExt.glBindBufferARB( GL_ARRAY_BUFFER_ARB, m_nTextureCoords );
	AssertNoGLError();
	glTexCoordPointer(2, GL_FLOAT, 0, NULL);
	AssertNoGLError();

	// TRICKY:  Don't bind and send normals if lighting is disabled.  This 
	// will save some effort transforming these values.
	GLboolean bLighting;
	glGetBooleanv( GL_LIGHTING, &bLighting );
	GLboolean bTextureGenS;
	glGetBooleanv( GL_TEXTURE_GEN_S, &bTextureGenS );
	GLboolean bTextureGenT;
	glGetBooleanv( GL_TEXTURE_GEN_T, &bTextureGenT );
	if( bLighting || bTextureGenS || bTextureGenT )
	{
		glEnableClientState(GL_NORMAL_ARRAY);
		GLExt.glBindBufferARB( GL_ARRAY_BUFFER_ARB, m_nNormals );
		AssertNoGLError();
		glNormalPointer(GL_FLOAT, 0, NULL);
		AssertNoGLError();
	}
	else
	{
		glDisableClientState(GL_NORMAL_ARRAY);
	}

	GLExt.glBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, m_nTriangles );
	AssertNoGLError();

#define BUFFER_OFFSET(o) ((char*)(o))

	ASSERT( GLExt.glDrawRangeElements);
	GLExt.glDrawRangeElements( 
		GL_TRIANGLES, 
		meshInfo.iVertexStart,	// minimum array index contained in indices
		meshInfo.iVertexStart+meshInfo.iVertexCount-1,
					// maximum array index contained in indices
		meshInfo.iTriangleCount*3,	// number of elements to be rendered
		GL_UNSIGNED_SHORT,
		BUFFER_OFFSET(meshInfo.iTriangleStart*sizeof(msTriangle)) );
	AssertNoGLError();
}

RageCompiledGeometry* RageDisplay_OGL::CreateCompiledGeometry()
{
	if( GLExt.glGenBuffersARB )
		return new RageCompiledGeometryHWOGL;
	else
		return new RageCompiledGeometrySWOGL;
}

void RageDisplay_OGL::DeleteCompiledGeometry( RageCompiledGeometry* p )
{
	delete p;
}

void RageDisplay_OGL::DrawQuadsInternal( const RageSpriteVertex v[], int iNumVerts )
{
	TurnOffHardwareVBO();
	SendCurrentMatrices();

	SetupVertices( v, iNumVerts );
	glDrawArrays( GL_QUADS, 0, iNumVerts );
}

void RageDisplay_OGL::DrawQuadStripInternal( const RageSpriteVertex v[], int iNumVerts )
{
	TurnOffHardwareVBO();
	SendCurrentMatrices();

	SetupVertices( v, iNumVerts );
	glDrawArrays( GL_QUAD_STRIP, 0, iNumVerts );
}

void RageDisplay_OGL::DrawFanInternal( const RageSpriteVertex v[], int iNumVerts )
{
	TurnOffHardwareVBO();

	glMatrixMode( GL_PROJECTION );

	SendCurrentMatrices();

	SetupVertices( v, iNumVerts );
	glDrawArrays( GL_TRIANGLE_FAN, 0, iNumVerts );
}

void RageDisplay_OGL::DrawStripInternal( const RageSpriteVertex v[], int iNumVerts )
{
	TurnOffHardwareVBO();
	SendCurrentMatrices();

	SetupVertices( v, iNumVerts );
	glDrawArrays( GL_TRIANGLE_STRIP, 0, iNumVerts );
}

void RageDisplay_OGL::DrawTrianglesInternal( const RageSpriteVertex v[], int iNumVerts )
{
	TurnOffHardwareVBO();
	SendCurrentMatrices();

	SetupVertices( v, iNumVerts );
	glDrawArrays( GL_TRIANGLES, 0, iNumVerts );
}

void RageDisplay_OGL::DrawCompiledGeometryInternal( const RageCompiledGeometry *p, int iMeshIndex )
{
	TurnOffHardwareVBO();
	SendCurrentMatrices();

	p->Draw( iMeshIndex );
}

void RageDisplay_OGL::DrawLineStripInternal( const RageSpriteVertex v[], int iNumVerts, float LineWidth )
{
	TurnOffHardwareVBO();

//	if( !GetVideoModeParams().bSmoothLines )
//	{
//		/* Fall back on the generic polygon-based line strip. */
//		RageDisplay::DrawLineStripInternal(v, iNumVerts, LineWidth );
//		return;
//	}

//the openGL non smooth lines look better than the "generic" ones, because they are
//"constant thickness" - the generic ones alternate between 1 and 2 pixels or so and look horrible
//i fixed this. --beware

	SendCurrentMatrices();

	/* Draw a nice AA'd line loop.  One problem with this is that point and line
	 * sizes don't always precisely match, which doesn't look quite right.
	 * It's worth it for the AA, though. */

	//if (GetVideoModeParams().bSmoothLines)
	//	glEnable(GL_LINE_SMOOTH);

	/* Our line width is wrt the regular internal SCREEN_WIDTHxSCREEN_HEIGHT screen,
	 * but these width functions actually want raster sizes (that is, actual pixels).
	 * Scale the line width and point size by the average ratio of the scale. */
	float WidthVal = float(wind->GetVideoModeParams().width) / SCREEN_WIDTH;
	float HeightVal = float(wind->GetVideoModeParams().height) / SCREEN_HEIGHT;
	LineWidth *= (WidthVal + HeightVal) / 2;

	/* Clamp the width to the hardware max for both lines and points (whichever
	 * is more restrictive). */
	LineWidth = clamp(LineWidth, g_line_range[0], g_line_range[1]);
	LineWidth = clamp(LineWidth, g_point_range[0], g_point_range[1]);

	/* Hmm.  The granularity of lines and points might be different; for example,
	 * if lines are .5 and points are .25, we might want to snap the width to the
	 * nearest .5, so the hardware doesn't snap them to different sizes.  Does it
	 * matter? */
	glLineWidth(LineWidth);

	/* Draw the line loop: */
	SetupVertices( v, iNumVerts );
	glDrawArrays( GL_LINE_STRIP, 0, iNumVerts );

	//if (GetVideoModeParams().bSmoothLines)
	//	glDisable(GL_LINE_SMOOTH);

	/* Round off the corners.  This isn't perfect; the point is sometimes a little
	 * larger than the line, causing a small bump on the edge.  Not sure how to fix
	 * that. */
	glPointSize(LineWidth);

	/* Hack: if the points will all be the same, we don't want to draw
	 * any points at all, since there's nothing to connect.  That'll happen
	 * if both scale factors in the matrix are ~0.  (Actually, I think
	 * it's true if two of the three scale factors are ~0, but we don't
	 * use this for anything 3d at the moment anyway ...)  This is needed
	 * because points aren't scaled like regular polys--a zero-size point
	 * will still be drawn. */
	RageMatrix mat;
	glGetFloatv( GL_MODELVIEW_MATRIX, (float*)mat );

	if(mat.m[0][0] < 1e-5 && mat.m[1][1] < 1e-5) 
		return;

	glEnable(GL_POINT_SMOOTH);

	SetupVertices( v, iNumVerts );
	glDrawArrays( GL_POINTS, 0, iNumVerts );

	glDisable(GL_POINT_SMOOTH);
}

void RageDisplay_OGL::ClearAllTextures()
{
	for( int i=0; i<MAX_TEXTURE_UNITS; i++ )
		SetTexture( i, NULL );

	// HACK:  Reset the active texture to 0.
	// TODO:  Change all texture functions to take a stage number.
	if( GLExt.glActiveTextureARB )
		GLExt.glActiveTextureARB(GL_TEXTURE0_ARB);
}

void RageDisplay_OGL::SetTexture( int iTextureUnitIndex, RageTexture* pTexture )
{
	if( GLExt.glActiveTextureARB == NULL )
	{
		// multitexture isn't supported.  Ignore all textures except for 0.
		if( iTextureUnitIndex != 0 )
			return;
	}
	else
	{
		switch( iTextureUnitIndex )
		{
		case 0:
			GLExt.glActiveTextureARB(GL_TEXTURE0_ARB);
			break;
		case 1:
			GLExt.glActiveTextureARB(GL_TEXTURE1_ARB);
			break;
		default:
			ASSERT(0);
		}
	}

	if( pTexture )
	{
		glEnable( GL_TEXTURE_2D );
		glBindTexture( GL_TEXTURE_2D, pTexture->GetTexHandle() );
	}
	else
	{
		glDisable( GL_TEXTURE_2D );
	}
}
void RageDisplay_OGL::SetTextureModeModulate()
{
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

void RageDisplay_OGL::SetTextureModeGlow(GlowMode m)
{
	if(m == GLOW_WHITEN && !g_bEXT_texture_env_combine)
		m = GLOW_BRIGHTEN; /* we can't do GLOW_WHITEN */

	switch(m)
	{
	case GLOW_BRIGHTEN:
		glBlendFunc( GL_SRC_ALPHA, GL_ONE );
		return;

	case GLOW_WHITEN:
		/* Source color is the diffuse color only: */
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_EXT);
		glTexEnvi(GL_TEXTURE_ENV, GLenum(GL_COMBINE_RGB_EXT), GL_REPLACE);
		glTexEnvi(GL_TEXTURE_ENV, GLenum(GL_SOURCE0_RGB_EXT), GL_PRIMARY_COLOR_EXT);

		/* Source alpha is texture alpha * diffuse alpha: */
		glTexEnvi(GL_TEXTURE_ENV, GLenum(GL_COMBINE_ALPHA_EXT), GL_MODULATE);
		glTexEnvi(GL_TEXTURE_ENV, GLenum(GL_OPERAND0_ALPHA_EXT), GL_SRC_ALPHA);
		glTexEnvi(GL_TEXTURE_ENV, GLenum(GL_SOURCE0_ALPHA_EXT), GL_PRIMARY_COLOR_EXT);
		glTexEnvi(GL_TEXTURE_ENV, GLenum(GL_OPERAND1_ALPHA_EXT), GL_SRC_ALPHA);
		glTexEnvi(GL_TEXTURE_ENV, GLenum(GL_SOURCE1_ALPHA_EXT), GL_TEXTURE);
		return;
	}
}

void RageDisplay_OGL::SetTextureModeAdd()
{
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
}

void RageDisplay_OGL::SetTextureFiltering( bool b )
{

}

void RageDisplay_OGL::SetBlendMode( BlendMode mode )
{
	glEnable(GL_BLEND);

	glDepthRange( 0.05, 1.0 );
	switch( mode )
	{
	case BLEND_NORMAL:
		glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		break;
	case BLEND_ADD:
		glBlendFunc( GL_SRC_ALPHA, GL_ONE );
		break;
	case BLEND_NO_EFFECT:
		/* XXX: Would it be faster and have the same effect to say glDisable(GL_COLOR_WRITEMASK)? */
		glBlendFunc( GL_ZERO, GL_ONE );

		/* This is almost exclusively used to draw masks to the Z-buffer.  Make sure
		 * masks always win the depth test when drawn at the same position. */
		glDepthRange( 0.0, 0.95 );	// this bias is bigger than it needs to be to handle the coplanar case
		break;
	default:
		ASSERT(0);
	}
}

bool RageDisplay_OGL::IsZWriteEnabled() const
{
	bool a;
	glGetBooleanv( GL_DEPTH_WRITEMASK, (unsigned char*)&a );
	return a;
}

bool RageDisplay_OGL::IsZTestEnabled() const
{
	GLenum a;
	glGetIntegerv( GL_DEPTH_FUNC, (GLint*)&a );
	return a != GL_ALWAYS;
}

void RageDisplay_OGL::ClearZBuffer()
{
	bool write = IsZWriteEnabled();
	SetZWrite( true );
    glClear( GL_DEPTH_BUFFER_BIT );
	SetZWrite( write );
}

void RageDisplay_OGL::SetZWrite( bool b )
{
	glDepthMask( b );
}

void RageDisplay_OGL::SetZTestMode( ZTestMode mode )
{
	glEnable( GL_DEPTH_TEST );
	switch( mode )
	{
	case ZTEST_OFF:				glDepthFunc( GL_ALWAYS );	break;
	case ZTEST_WRITE_ON_PASS:	glDepthFunc( GL_LEQUAL );	break;
	case ZTEST_WRITE_ON_FAIL:	glDepthFunc( GL_GREATER );	break;
	default:	ASSERT( 0 );
	}
}

void RageDisplay_OGL::SetTextureWrapping( bool b )
{
	GLenum mode = b ? GL_REPEAT : GL_CLAMP_TO_EDGE;
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, mode );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, mode );
}

void RageDisplay_OGL::SetMaterial( 
	const RageColor &emissive,
	const RageColor &ambient,
	const RageColor &diffuse,
	const RageColor &specular,
	float shininess
	)
{
	// TRICKY:  If lighting is off, then setting the material 
	// will have no effect.  Even if lighting is off, we still
	// want Models to have basic color and transparency.
	// We can do this fake lighting by setting the vertex color.

	GLboolean bLighting;
	glGetBooleanv( GL_LIGHTING, &bLighting );

	if( bLighting )
	{
		glMaterialfv( GL_FRONT, GL_EMISSION, emissive );
		glMaterialfv( GL_FRONT, GL_AMBIENT, ambient );
		glMaterialfv( GL_FRONT, GL_DIFFUSE, diffuse );
		glMaterialfv( GL_FRONT, GL_SPECULAR, specular );
		glMaterialf( GL_FRONT, GL_SHININESS, shininess );
	}
	else
	{
		glColor4fv( emissive + ambient + diffuse );
	}
}

void RageDisplay_OGL::SetLighting( bool b )
{
	if( b )	glEnable( GL_LIGHTING );
	else	glDisable( GL_LIGHTING );
}

void RageDisplay_OGL::SetLightOff( int index )
{
	glDisable( GL_LIGHT0+index );
}
void RageDisplay_OGL::SetLightDirectional( 
	int index, 
	const RageColor &ambient, 
	const RageColor &diffuse, 
	const RageColor &specular, 
	const RageVector3 &dir )
{
	// Light coordinates are transformed by the modelview matrix, but
	// we are being passed in world-space coords.
	glPushMatrix();
	glLoadIdentity();

	glEnable( GL_LIGHT0+index );
	glLightfv(GL_LIGHT0+index, GL_AMBIENT, ambient);
	glLightfv(GL_LIGHT0+index, GL_DIFFUSE, diffuse);
	glLightfv(GL_LIGHT0+index, GL_SPECULAR, specular);
	float position[4] = {dir.x, dir.y, dir.z, 0};
	glLightfv(GL_LIGHT0+index, GL_POSITION, position);

	glPopMatrix();
}

void RageDisplay_OGL::SetCullMode( CullMode mode )
{
	switch( mode )
	{
	case CULL_BACK:
		glEnable( GL_CULL_FACE );
		glCullFace( GL_BACK );
		break;
	case CULL_FRONT:
		glEnable( GL_CULL_FACE );
		glCullFace( GL_FRONT );
		break;
	case CULL_NONE:
        glDisable( GL_CULL_FACE );
		break;
	default:
		ASSERT(0);
	}
}

const RageDisplay::PixelFormatDesc *RageDisplay_OGL::GetPixelFormatDesc(PixelFormat pf) const
{
	ASSERT( pf < NUM_PIX_FORMATS );
	return &PIXEL_FORMAT_DESC[pf];
}

void RageDisplay_OGL::DeleteTexture( unsigned uTexHandle )
{
	unsigned int uTexID = uTexHandle;

	FlushGLErrors();
	glDeleteTextures(1,reinterpret_cast<GLuint*>(&uTexID));

	AssertNoGLError();
}


RageDisplay::PixelFormat RageDisplay_OGL::GetImgPixelFormat( RageSurface* &img, bool &FreeImg, int width, int height )
{
	PixelFormat pixfmt = FindPixelFormat( img->format->BitsPerPixel, img->format->Rmask, img->format->Gmask, img->format->Bmask, img->format->Amask );
	
	if( pixfmt == NUM_PIX_FORMATS || !SupportsSurfaceFormat(pixfmt) )
	{
		/* The source isn't in a supported, known pixel format.  We need to convert
		 * it ourself.  Just convert it to RGBA8, and let OpenGL convert it back
		 * down to whatever the actual pixel format is.  This is a very slow code
		 * path, which should almost never be used. */
		pixfmt = FMT_RGBA8;
		ASSERT( SupportsSurfaceFormat(pixfmt) );

		const PixelFormatDesc *pfd = DISPLAY->GetPixelFormatDesc(pixfmt);

		RageSurface *imgconv = CreateSurface( width, height,
			pfd->bpp, pfd->masks[0], pfd->masks[1], pfd->masks[2], pfd->masks[3] );
		RageSurfaceUtils::Blit( img, imgconv, width, height );
		img = imgconv;
		FreeImg = true;
	}
	else
		FreeImg = false;

	return pixfmt;
}

unsigned RageDisplay_OGL::CreateTexture( 
	PixelFormat pixfmt,
	RageSurface* img,
	bool bGenerateMipMaps )
{
	ASSERT( pixfmt < NUM_PIX_FORMATS );
	ASSERT( img->w == power_of_two(img->w) && img->h == power_of_two(img->h) );


	/* Find the pixel format of the image we've been given. */
	bool FreeImg;
	PixelFormat imgpixfmt = GetImgPixelFormat( img, FreeImg, img->w, img->h );

	GLenum glTexFormat = GL_PIXFMT_INFO[pixfmt].internalfmt;
	GLenum glImageFormat = GL_PIXFMT_INFO[imgpixfmt].format;
	GLenum glImageType = GL_PIXFMT_INFO[imgpixfmt].type;

	// HACK:  OpenGL 1.2 types aren't available in GLU 1.3.  Don't call GLU for mip
	// mapping if we're using an OGL 1.2 type and don't have >= GLU 1.3.
	// http://pyopengl.sourceforge.net/documentation/manual/gluBuild2DMipmaps.3G.html
	if( bGenerateMipMaps && g_gluVersion < 13 )
	{
		switch( pixfmt )
		{
		// OpenGL 1.1 types
		case FMT_RGBA8:
		case FMT_RGB8:
		case FMT_PAL:
		case FMT_BGR8:
			break;
		// OpenGL 1.2 types
		default:
			LOG->Trace( "Can't generate mipmaps for type %s because GLU version %.1f is too old.", GLToString(glImageType).c_str(), g_gluVersion/10.f );
			bGenerateMipMaps = false;
			break;
		}
	}

	// allocate OpenGL texture resource
	unsigned int uTexHandle;
	glGenTextures(1, reinterpret_cast<GLuint*>(&uTexHandle));
	ASSERT(uTexHandle);
	
	glBindTexture( GL_TEXTURE_2D, uTexHandle );
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	GLint minFilter;
	if( bGenerateMipMaps )
	{
		if( wind->GetVideoModeParams().bTrilinearFiltering )
			minFilter = GL_LINEAR_MIPMAP_LINEAR;
		else
			minFilter = GL_LINEAR_MIPMAP_NEAREST;
	}
	else
	{
		minFilter = GL_LINEAR;
	}
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);

	if( wind->GetVideoModeParams().bAnisotropicFiltering &&
		HasExtension("GL_EXT_texture_filter_anisotropic") )
	{
		GLfloat largest_supported_anisotropy;
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &largest_supported_anisotropy);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, largest_supported_anisotropy);
	}

	SetTextureWrapping( false );

	glPixelStorei(GL_UNPACK_ROW_LENGTH, img->pitch / img->format->BytesPerPixel);


	if(pixfmt == FMT_PAL)
	{
		/* The image is paletted.  Let's try to set up a paletted texture. */
		GLubyte palette[256*4];
		memset(palette, 0, sizeof(palette));
		int p = 0;
		/* Copy the palette to the simple, unpacked data OGL expects. If
		 * we're color keyed, change it over as we go. */
		for(int i = 0; i < img->format->palette->ncolors; ++i)
		{
			palette[p++] = img->format->palette->colors[i].r;
			palette[p++] = img->format->palette->colors[i].g;
			palette[p++] = img->format->palette->colors[i].b;
			palette[p++] = img->format->palette->colors[i].a;
		}

		/* Set the palette. */
		GLExt.glColorTableEXT(GL_TEXTURE_2D, GL_RGBA8, 256, GL_RGBA, GL_UNSIGNED_BYTE, palette);

		GLint RealFormat = 0;
		GLExt.glGetColorTableParameterivEXT(GL_TEXTURE_2D, GL_COLOR_TABLE_FORMAT, &RealFormat);
		ASSERT( RealFormat == GL_RGBA8);	/* This is a case I don't expect to happen. */
	}


	
	FlushGLErrors();

	if( bGenerateMipMaps )
	{
		GLenum error = gluBuild2DMipmaps(
			GL_TEXTURE_2D, glTexFormat, 
			img->w, img->h,
			glImageFormat, glImageType, img->pixels );

		if( error != 0 )
		{
			ostringstream s;
			s << "gluBuild2DMipmaps(format " << GLToString(glTexFormat) <<
				 ", w " << img->w << ", h " <<  img->h <<
				 ", format " << GLToString(glImageFormat) <<
				 ", type " << GLToString(glImageType) <<
				 "): " << gluErrorString(error);
			LOG->Trace( s.str().c_str() );

			ASSERT(0);
		}
	}
	else
	{
		glTexImage2D(
			GL_TEXTURE_2D, 0, glTexFormat, 
			img->w, img->h, 0,
			glImageFormat, glImageType, img->pixels);
		
		{
			ostringstream s;
			s << "glTexImage2D(format " << GLToString(glTexFormat) <<
					", " << img->w << "x" <<  img->h <<
					", format " << GLToString(glImageFormat) <<
					", type " << GLToString(glImageType) <<
					", pixfmt " << pixfmt <<
					", imgpixfmt " << imgpixfmt;
			LOG->Trace( s.str().c_str() );
		}
		GLenum error = glGetError();
		if( error != GL_NO_ERROR )
		{
			ostringstream s;
			s << "glTexImage2D(format " << GLToString(glTexFormat) <<
				 ", " << img->w << "x" <<  img->h <<
				 ", format " << GLToString(glImageFormat) <<
				 ", type " << GLToString(glImageType) <<
				 ", pixfmt " << pixfmt <<
				 ", imgpixfmt " << imgpixfmt <<
				 "): " << GLToString(error);
			LOG->Trace( s.str().c_str() );

			ASSERT(0);
		}
	}



	/* Sanity check: */
	if( pixfmt == FMT_PAL )
	{
		GLint size = 0;
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GLenum(GL_TEXTURE_INDEX_SIZE_EXT), &size);
		if(size != 8)
			RageException::Throw("Thought paletted textures worked, but they don't.");
	}

	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glFlush();

	if( FreeImg )
		delete img;
	return uTexHandle;
}


void RageDisplay_OGL::UpdateTexture( 
	unsigned uTexHandle, 
	RageSurface* img,
	int xoffset, int yoffset, int width, int height )
{
	glBindTexture( GL_TEXTURE_2D, uTexHandle );

	bool FreeImg;
	PixelFormat pixfmt = GetImgPixelFormat( img, FreeImg, width, height );

	glPixelStorei(GL_UNPACK_ROW_LENGTH, img->pitch / img->format->BytesPerPixel);

//	GLenum glTexFormat = GL_PIXFMT_INFO[pixfmt].internalfmt;
	GLenum glImageFormat = GL_PIXFMT_INFO[pixfmt].format;
	GLenum glImageType = GL_PIXFMT_INFO[pixfmt].type;

	glTexSubImage2D(GL_TEXTURE_2D, 0,
		xoffset, yoffset,
		width, height,
		glImageFormat, glImageType, img->pixels);

	/* Must unset PixelStore when we're done! */
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glFlush();

	if( FreeImg )
		delete img;
}

void RageDisplay_OGL::SetPolygonMode( PolygonMode pm )
{
	GLenum m;
	switch( pm )
	{
	case POLYGON_FILL:	m = GL_FILL; break;
	case POLYGON_LINE:	m = GL_LINE; break;
	default:	ASSERT(0);	return;
	}
	glPolygonMode( GL_FRONT_AND_BACK, m );
}

void RageDisplay_OGL::SetLineWidth( float fWidth )
{
	glLineWidth( fWidth );
}

CString RageDisplay_OGL::GetTextureDiagnostics( unsigned id ) const
{
	return "";
}

void RageDisplay_OGL::SetAlphaTest( bool b )
{
	glAlphaFunc( GL_GREATER, 0.01f );
	if( b )
		glEnable( GL_ALPHA_TEST );
	else
		glDisable( GL_ALPHA_TEST );
}

RageMatrix RageDisplay_OGL::GetOrthoMatrix( float l, float r, float b, float t, float zn, float zf )
{
	RageMatrix m(
		2/(r-l),      0,            0,           0,
		0,            2/(t-b),      0,           0,
		0,            0,            -2/(zf-zn),   0,
		-(r+l)/(r-l), -(t+b)/(t-b), -(zf+zn)/(zf-zn),  1 );
	return m;
}


/*
 * Although we pair texture formats (eg. GL_RGB8) and surface formats
 * (pairs of eg. GL_RGB8,GL_UNSIGNED_SHORT_5_5_5_1), it's possible for
 * a format to be supported for a texture format but not a surface
 * format.  This is abstracted, so you don't need to know about this
 * as a user calling CreateTexture.
 *
 * One case of this is if packed pixels aren't supported.  We can still
 * use 16-bit color modes, but we have to send it in 32-bit.  Almost
 * everything supports packed pixels.
 *
 * Another case of this is incomplete packed pixels support.  Some implementations
 * neglect GL_UNSIGNED_SHORT_*_REV. 
 */
bool RageDisplay_OGL::SupportsSurfaceFormat( PixelFormat pixfmt )
{
	switch( GL_PIXFMT_INFO[pixfmt].type )
	{
	case GL_UNSIGNED_SHORT_1_5_5_5_REV:
		return g_bGL_EXT_bgra && g_bReversePackedPixelsWorks;
	default:
		return true;
	}
}


bool RageDisplay_OGL::SupportsTextureFormat( PixelFormat pixfmt, bool realtime )
{
	/* If we support a pixfmt for texture formats but not for surface formats, then
	 * we'll have to convert the texture to a supported surface format before uploading.
	 * This is too slow for dynamic textures. */
	if( realtime && !SupportsSurfaceFormat( pixfmt ) )
		return false;

	switch( GL_PIXFMT_INFO[pixfmt].format )
	{
	case GL_COLOR_INDEX:
		return GLExt.glColorTableEXT && GLExt.glGetColorTableParameterivEXT;
	case GL_BGR:
	case GL_BGRA:
		return g_bGL_EXT_bgra;
	default:
		return true;
	}
}

void RageDisplay_OGL::SetSphereEnironmentMapping( bool b )
{
	if( b )
	{
		glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
		glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
		glEnable(GL_TEXTURE_GEN_S);
		glEnable(GL_TEXTURE_GEN_T);
	}
	else
	{
		glDisable(GL_TEXTURE_GEN_S);
		glDisable(GL_TEXTURE_GEN_T);
	}
}

/*
 * Copyright (c) 2001-2004 Chris Danford, Glenn Maynard
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

