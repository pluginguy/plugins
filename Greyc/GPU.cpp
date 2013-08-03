#include "GPU.h"
#include "Helpers.h"

#define EXTERN
#include "GPUExtensions.h"
#undef EXTERN

#pragma comment(lib, "opengl32.lib")

static const char *szClassName = "Greycstoration OpenGL window class";
int GPU::m_iClassRefcount = 0;

GPU::GPU()
{
	m_bClassRegistered = false;
	m_bwglCurrent = false;
	m_hWnd = NULL;
	m_HDC = NULL;
	m_HGLRC = NULL;
}

GPU::~GPU()
{
	Destroy();
}

void GPU::Init()
{
	if(m_HGLRC != NULL)
		return;

	try
	{
		Instance hInstance;
		if(!m_bClassRegistered)
		{
			if(m_iClassRefcount == 0)
			{
				WNDCLASS WindowClass =
				{
					CS_OWNDC | CS_BYTEALIGNCLIENT, DefWindowProc, 0, 0, hInstance.Get(), NULL, NULL, NULL, NULL, szClassName
				}; 

				if(!RegisterClass(&WindowClass))
					throw Exception(StringUtil::ssprintf("RegisterClass: %s", StringUtil::GetWindowsError(GetLastError()).c_str()));
			}
			m_bClassRegistered = true;
			++m_iClassRefcount;
		}

		if(m_hWnd != NULL)
			return;

		m_hWnd = CreateWindow(szClassName, "Greycstoration OpenGL window", 0, 0, 0, 0, 0, NULL, NULL, hInstance.Get(), NULL);
		if(m_hWnd == NULL)
			throw Exception(StringUtil::ssprintf("CreateWindow %i: %s", GetLastError(), StringUtil::GetWindowsError(GetLastError()).c_str()));

		m_HDC = GetDC(m_hWnd);

		PIXELFORMATDESCRIPTOR PixelFormat = { 0 };
		PixelFormat.nSize		= sizeof(PIXELFORMATDESCRIPTOR);
		PixelFormat.nVersion		= 1;
		PixelFormat.dwFlags		= PFD_DRAW_TO_WINDOW | /*PFD_DOUBLEBUFFER | */PFD_SUPPORT_OPENGL;
		PixelFormat.iPixelType		= PFD_TYPE_RGBA;
		PixelFormat.cColorBits		= 24;
		PixelFormat.cDepthBits		= 16;

		int iPixelFormat = ChoosePixelFormat(m_HDC, &PixelFormat);
		if(iPixelFormat == 0)
			throw Exception("ChoosePixelFormat");

		/* Set the pixel format. */
		if(!SetPixelFormat(m_HDC, iPixelFormat, &PixelFormat))
			throw Exception(StringUtil::ssprintf("SetPixelFormat: %s", StringUtil::GetWindowsError(GetLastError()).c_str()));

		if(m_HGLRC == NULL)
		{
			m_HGLRC = wglCreateContext(m_HDC);
			if(m_HGLRC == NULL)
				throw Exception(StringUtil::ssprintf("wglCreateContext: %s", StringUtil::GetWindowsError(GetLastError()).c_str()));
		}

		if(!wglMakeCurrent(m_HDC, m_HGLRC))
			throw Exception(StringUtil::ssprintf("wglMakeCurrent: %s", StringUtil::GetWindowsError(GetLastError()).c_str()));
		m_bwglCurrent = true;

		printf("OpenGL vendor: %s\n", glGetString(GL_VENDOR));
		printf("Renderer: %s\n", glGetString(GL_RENDERER));

		if(!strcmp((const char*) glGetString(GL_RENDERER), "GDI Generic"))
			throw Exception("Unaccelerated OpenGL context received");

		LoadExtensions();
	} catch(...) {
		Destroy();
		throw;
	}
}

void GPU::Destroy()
{
	if(m_bwglCurrent)
	{
		wglMakeCurrent(NULL, NULL);
		m_bwglCurrent = false;
	}

	if(m_HDC != NULL)
	{
		ReleaseDC(m_hWnd, m_HDC);
		m_HDC = NULL;
	}

	if(m_hWnd != NULL)
	{
		DestroyWindow(m_hWnd);
		m_hWnd = NULL;
	}

	if(m_HGLRC != NULL)
	{
		wglDeleteContext(m_HGLRC);
		m_HGLRC = NULL;
	}

	if(m_bClassRegistered)
	{
		m_bClassRegistered = false;
		--m_iClassRefcount;
		if(!m_iClassRefcount)
		{
			Instance hInstance;
			UnregisterClass(szClassName, hInstance.Get());
		}
	}
}

void GPU::LoadExtensions()
{
	const char *szRequiredExtensions[] =
	{
		"GL_EXT_framebuffer_object",
		"GL_ARB_shader_objects",
		"GL_ARB_fragment_program",
		"GL_ARB_multitexture",
		NULL
	};

	for(int i = 0; szRequiredExtensions[i]; ++i)
	{
		if(!strstr((const char *) glGetString(GL_EXTENSIONS), szRequiredExtensions[i]))
			throw Exception(StringUtil::ssprintf("GPU support unavailable; missing extension %s", szRequiredExtensions[i]));
	}

	struct Extension
	{
		void **p;
		const char *szName;
	};
#define EXT(name) { (void **) &name, #name }

	Extension exts[] =
	{
                EXT(glBindFramebufferEXT),
                EXT(glBindRenderbufferEXT),
                EXT(glCheckFramebufferStatusEXT),
                EXT(glDeleteFramebuffersEXT),
                EXT(glDeleteRenderbuffersEXT),
                EXT(glFramebufferRenderbufferEXT),
                EXT(glFramebufferTexture1DEXT),
                EXT(glFramebufferTexture2DEXT),
                EXT(glFramebufferTexture3DEXT),
                EXT(glGenerateMipmapEXT),
                EXT(glGenFramebuffersEXT),
                EXT(glGenRenderbuffersEXT),
                EXT(glGetFramebufferAttachmentParameterivEXT),
                EXT(glGetRenderbufferParameterivEXT),
                EXT(glIsFramebufferEXT),
                EXT(glIsRenderbufferEXT),
                EXT(glRenderbufferStorageEXT),
		EXT(glCreateShaderObjectARB),
		EXT(glCreateShaderObjectARB),
		EXT(glCreateProgramObjectARB),
		EXT(glShaderSourceARB),
		EXT(glCompileShaderARB),
		EXT(glGetObjectParameterfvARB),
		EXT(glGetObjectParameterivARB),
		EXT(glGetInfoLogARB),
		EXT(glAttachObjectARB),
		EXT(glDeleteObjectARB),
		EXT(glLinkProgramARB),
		EXT(glUseProgramObjectARB),
		EXT(glVertexAttrib2fARB),
		EXT(glVertexAttrib3fARB),
		EXT(glVertexAttrib4fARB),
		EXT(glEnableVertexAttribArrayARB),
		EXT(glDisableVertexAttribArrayARB),
		EXT(glVertexAttribPointerARB),
		EXT(glGetUniformLocationARB),
		EXT(glUniform1fARB),
		EXT(glUniform2fARB),
		EXT(glUniform3fARB),
		EXT(glUniform4fARB),
		EXT(glUniform1iARB),
		EXT(glUniform2iARB),
		EXT(glUniform3iARB),
		EXT(glUniform4iARB),
		EXT(glUniform1fvARB),
		EXT(glUniform2fvARB),
		EXT(glUniform3fvARB),
		EXT(glUniform4fvARB),
		EXT(glUniform1ivARB),
		EXT(glUniform2ivARB),
		EXT(glUniform3ivARB),
		EXT(glUniform4ivARB),
		EXT(glUniformMatrix2fvARB),
		EXT(glUniformMatrix2fvARB),
		EXT(glUniformMatrix2fvARB),
		EXT(glGetActiveUniformARB),
		EXT(glActiveTextureARB),
		EXT(glBlendEquation),
		EXT(glClampColorARB),
		{ NULL, NULL }
        };

	for(int i = 0; exts[i].p; ++i)
	{
		*exts[i].p = wglGetProcAddress(exts[i].szName);
		if(*exts[i].p == NULL)
			throw Exception(StringUtil::ssprintf("Missing OpenGL symbol %s", exts[i].szName));
	}
}

string GPU::InfoLog(GLhandleARB h)
{
	GLint iSize;
	glGetObjectParameterivARB(h, GL_OBJECT_INFO_LOG_LENGTH_ARB, &iSize);
	if(!iSize)
		return "";

	GLcharARB *pInfoLog = new GLcharARB[iSize];
	glGetInfoLogARB(h, iSize, &iSize, pInfoLog);
	string sLog = pInfoLog;
	delete[] pInfoLog;
	return sLog;
}

/* 
 * If bTest is true, this is a test compile.  On success, return 1; on error, return 0 and don't raise an exception.
 * The resulting program object is deleted and not returned.
 */
bool GPU::TestCompileShader(string sShaderName, string sSource)
{
	try
	{
		GLhandleARB hShader = CompileShader(sShaderName, sSource);
		glDeleteObjectARB(hShader);
		return true;
	}
	catch(const Exception &)
	{
		return false;
	}
}

GLhandleARB GPU::CompileShader(string sShaderName, string sSource)
{
	GLhandleARB hShader = glCreateShaderObjectARB(GL_FRAGMENT_SHADER_ARB);

	int iSize = sSource.size();
	const char *szSource = sSource.c_str();
	glShaderSourceARB(hShader, 1, &szSource, &iSize);
	glCompileShaderARB(hShader);

	string sLog = InfoLog(hShader);

	GLint bStatus = GL_FALSE;
	glGetObjectParameterivARB(hShader, GL_OBJECT_COMPILE_STATUS_ARB, &bStatus);
	if(!bStatus)
	{
		printf("Error compiling %s:\n%s", sShaderName.c_str(), sLog.c_str());
		glDeleteObjectARB(hShader);
		throw Exception(StringUtil::ssprintf("Error compiling %s: %s", sShaderName.c_str(), sLog.c_str()));
	}

	if(!sLog.empty())
		printf("Message compiling %s:\n%s", sShaderName.c_str(), sLog.c_str());

	GLhandleARB hProgram = glCreateProgramObjectARB();
	glAttachObjectARB(hProgram, hShader);
	glDeleteObjectARB(hShader);
	glLinkProgramARB(hProgram);
	glGetObjectParameterivARB(hProgram, GL_OBJECT_LINK_STATUS_ARB, &bStatus);
	if(!bStatus)
	{
		sLog = InfoLog(hProgram);
		printf("Error linking %s: %s", sShaderName.c_str(), sLog.c_str());
		glDeleteObjectARB(hProgram);
		throw Exception(StringUtil::ssprintf("Error compiling shader: %s", sLog.c_str()));
	}
	return hProgram;
}

void GPU::Draw(float fWidth, float fHeight)
{
	glBegin(GL_QUADS);
	glTexCoord2f(0,		0);		glVertex2f(0,0);
	glTexCoord2f(fWidth,	0);		glVertex2f(1,0);
	glTexCoord2f(fWidth,	fHeight);	glVertex2f(1,1);
	glTexCoord2f(0,		fHeight);	glVertex2f(0,1);
	glEnd();
}

void GL::glSetUniform1i(GLhandleARB programObj, const char *szName, int i1)
{
	int iParam = glGetUniformLocationARB(programObj, szName);
	if(iParam != -1)
		glUniform1iARB(iParam, i1);
}

void GL::glSetUniform1f(GLhandleARB programObj, const char *szName, float f1)
{
	int iParam = glGetUniformLocationARB(programObj, szName);
	if(iParam != -1)
		glUniform1fARB(iParam, f1);
}

void GL::glSetUniform2f(GLhandleARB programObj, const char *szName, float f1, float f2)
{
	int iParam = glGetUniformLocationARB(programObj, szName);
	if(iParam != -1)
		glUniform2fARB(iParam, f1, f2);
}

void GL::glSetUniform3f(GLhandleARB programObj, const char *szName, float f1, float f2, float f3)
{
	int iParam = glGetUniformLocationARB(programObj, szName);
	if(iParam != -1)
		glUniform3fARB(iParam, f1, f2, f3);
}

void GL::glSetUniform4f(GLhandleARB programObj, const char *szName, float f1, float f2, float f3, float f4)
{
	int iParam = glGetUniformLocationARB(programObj, szName);
	if(iParam != -1)
		glUniform4fARB(iParam, f1, f2, f3, f4);
}

void GL::glBindTextures(GLuint handle1, GLuint handle2, GLuint handle3, GLuint handle4)
{
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, handle1);
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, handle2);
	glActiveTextureARB(GL_TEXTURE2_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, handle3);
	glActiveTextureARB(GL_TEXTURE3_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, handle4);

	glActiveTextureARB(GL_TEXTURE0_ARB);
}
/*
void GL::glSetDrawBuffers(GLenum handle1, GLenum handle2, GLenum handle3, GLenum handle4)
{
	GLenum iOutputBuffer[] = { handle1, handle2, handle3, handle4, 0 };
	int iCount = 0;
	while(iOutputBuffer[iCount])
		++iCount;
	glDrawBuffersARB(iCount, iOutputBuffer);
}
*/
GPUTexture::GPUTexture()
{
	glGenTextures(1, (GLuint *) &m_iHandle);
}

void GPUTexture::Destroy()
{
	glDeleteTextures(1, (GLuint*) &m_iHandle);
	m_iHandle = 0;
}

void GPUTexture::SetDefaults(bool bNearest)
{
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, bNearest? GL_NEAREST:GL_LINEAR);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, bNearest? GL_NEAREST:GL_LINEAR);
}

/* debug: */
#include "CImgI.h"
void GPUTexture::ExamineTexture(int iHandle)
{
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, iHandle);
	int iWidth;
	glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB, 0, GL_TEXTURE_WIDTH, &iWidth);
	int iHeight;
	glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB, 0, GL_TEXTURE_HEIGHT, &iHeight);

	float *pMap = new float[iWidth * iHeight * 4];
	glGetTexImage(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, GL_FLOAT, pMap);

	CImgF f;
	f.Hold(pMap, iWidth, iHeight, 4, iWidth * 4);

	float *p = pMap;
	printf("********** -> %f %f %f %f\n",
		f(5,0,0),
		f(5,0,1),
		f(5,0,2),
		f(5,0,3));
	for(int y = 0; y < iHeight; ++y)
	{
		for(int x = 0; x < iWidth; ++x)
		{
			float r = *(p++);
			float g = *(p++);
			float b = *(p++);
			float a = *(p++);
//			if(f > 10)
//				printf("*** %f, %f, %f, %f\n", r, g, b, a);
			// p+=1;
		}
	}
	delete[] pMap;
}

GPUFrameBuffer::GPUFrameBuffer()
{
	glGenFramebuffersEXT(1, (GLuint *) &m_iHandle);
}

void GPUFrameBuffer::Destroy()
{
	glDeleteFramebuffersEXT(1, (GLuint*) &m_iHandle);
	m_iHandle = 0;
}

void GPUFrameBuffer::Check(const char *szName)
{
	GLenum iStatus = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
	if(iStatus != GL_FRAMEBUFFER_COMPLETE_EXT)
		throw(Exception(StringUtil::ssprintf("FBO: incomplete framebuffer for %s: %04x", szName, iStatus)));
}

GPUShader::GPUShader()
{
	m_iHandle = NULL;
}

void GPUShader::Destroy()
{
	if(m_iHandle != NULL)
		glDeleteObjectARB(m_iHandle);
	m_iHandle = NULL;
}
