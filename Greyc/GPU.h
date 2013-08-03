#ifndef GPU_H
#define GPU_H

#include <windows.h>
#include <GL/gl.h>
#include <string>
#include "glext.h"
using namespace std;

#define EXTERN extern
#include "GPUExtensions.h"
#undef EXTERN

class GPU
{
public:
	GPU();
	~GPU();
	void Init();
	void Destroy();
	void Draw(float fWidth, float fHeight);

	bool Initialized() const { return m_HGLRC != NULL; }
	HDC GetHDC() const { return m_HDC; }
	HWND GetHwnd() const { return m_hWnd; }

	string InfoLog(GLhandleARB h);
	GLhandleARB CompileShader(string sShaderName, string sSource);
	bool TestCompileShader(string sShaderName, string sSource);

private:
	void LoadExtensions();

	HWND m_hWnd;
	HDC m_HDC;
	HGLRC m_HGLRC;
	bool m_bClassRegistered;
	static int m_iClassRefcount;
	bool m_bwglCurrent;
};

class GPUTexture
{
public:
	GPUTexture();
	~GPUTexture() { Destroy(); }
	void Destroy();
	static void SetDefaults(bool bNearest);
	static void ExamineTexture(int iHandle);

	int m_iHandle;
};

class GPUFrameBuffer
{
public:
	GPUFrameBuffer();
	~GPUFrameBuffer() { Destroy(); }
	void Destroy();
	static void Check(const char *szName);

	int m_iHandle;
};

class GPUShader
{
public:
	GPUShader();
	~GPUShader() { Destroy(); }
	void Destroy();

	GLhandleARB m_iHandle;
private:
	GPUShader(const GPUShader &cpy); // don't use
};

namespace GL
{
	void glSetUniform1i(GLhandleARB programObj, const char *szName, int i1);
	void glSetUniform1f(GLhandleARB programObj, const char *szName, float f1);
	void glSetUniform2f(GLhandleARB programObj, const char *szName, float f1, float f2);
	void glSetUniform3f(GLhandleARB programObj, const char *szName, float f1, float f2, float f3);
	void glSetUniform4f(GLhandleARB programObj, const char *szName, float f1, float f2, float f3, float f4);
	void glBindTextures(GLuint handle1 = 0, GLuint handle2 = 0, GLuint handle3 = 0, GLuint handle4 = 0);
	void glSetDrawBuffers(GLenum handle1 = 0, GLenum handle2 = 0, GLenum handle3 = 0, GLenum handle4 = 0);
}

#endif
