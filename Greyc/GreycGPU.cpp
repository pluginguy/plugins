#include "GreycGPU.h"
#include "Helpers.h"
#include <math.h>
#include <assert.h>

/*
 * OpenGL acceleration.
 *
 * This is not the complete algorithm.  Other parts are handled by GreycC.cpp.
 *
 * This can handle up to four channels.  This is enough for almost any practical
 * image that you'd want to denoise: RGB, CMYK, LAB.
 *
 * Photoshop non-background layers in CMYK have an alpha channel, leading to CMYKA;
 * if you pass that in, only the first four channels will be processed.  (The same
 * limitations apply to the SSE-optimized parts of the code.)  This doesn't seem to
 * be an interesting limitation; if a scan or a photograph has an alpha channel, it
 * was almost always added later and doesn't need to be denoised.  If you really want
 * to denoise more than four channels, select the individual channels to denoise and
 * run separate passes.
 *
 * Cleanup must be called from the same thread as ProcessAnisotropic.  ProcessAnisotropic
 * can be used any number of times to avoid repeated startup costs.
 */
#pragma warning (disable : 4244) // 'initializing' : conversion from 'int' to 'const float', possible loss of data
#pragma warning (disable : 4101) // unreferenced local variable

void GreycGPU::InitShaders()
{
	bool bMissingAtan2 = false;
	if(IIRShader.m_iHandle == 0)
	{
		if(!gpu.TestCompileShader("atan2 test", "void main(void) { atan2(1.0, 1.0); }"))
		{
			bMissingAtan2 = true;
			printf("Enabling ATI atan2 workaround\n");
		}

		/*
		 * Implement:
		 * const float xc = img(x,y,z,v);
		 * const float xp = img(x-1, y,z,v);
		 * const float yp = prev(x-1,y,z,v);
		 * const float yb = prev(x-2,y,z,v);
		 * result = a0*xc + a1*xp - b1*yp - b2*yb;
		 *
		 * where prev is the output of the previous column.  This needs to be ping-ponged for
		 * each column.
		 *
		 * dir.xy is the step to the previous column (or row).  dir.zw = dir.xy * 2, to step
		 * to the column before that.
		 * w.xyzw = { a0, a1, b1, b2 }.
		 */

		string sIIRProgram =
"#extension GL_ARB_texture_rectangle : require\n"
"uniform sampler2DRect img; \
uniform sampler2DRect prev; \
uniform vec4 dir; \
uniform vec4 w; \
void main(void) \
{ \
	vec4 xc = texture2DRect(img, gl_TexCoord[0].xy); \
	vec4 xp = texture2DRect(img, gl_TexCoord[0].xy + dir.xy); \
	vec4 yp = texture2DRect(prev, gl_TexCoord[0].xy + dir.xy); \
	vec4 yb = texture2DRect(prev, gl_TexCoord[0].xy + dir.zw); \
	gl_FragColor = w.x*xc + w.y*xp - w.z*yp - w.w*yb; \
}";
		IIRShader.m_iHandle = gpu.CompileShader("IIR shader", sIIRProgram);
}


	if(EigenShader.m_iHandle == 0)
	{
		string sEigenProgram =
"#extension GL_ARB_texture_rectangle : require\n"
"uniform sampler2DRect G; \
uniform sampler2D atan2_texture; \
uniform float power1, power2;";

		if(bMissingAtan2)
		{
			/* We're on a GPU that's missing atan2.  Implement an approximation.
			 * Public domain: http://www.dspguru.com/dsp/tricks/fixed-point-atan2-with-self-normalization. */
			sEigenProgram +=
"float atan2(float y, float x) \
{ \
	float coeff_1 = 3.14159265358 / 4.0; \
	float coeff_2 = 3.0 * coeff_1; \
	float abs_y = abs(y); \
	float angle; \
	if (x >= 0.0) { \
		float r = (x - abs_y) / (abs_y + x); \
		angle = coeff_1 - coeff_1 * r; \
	} else { \
		float r = (x + abs_y) / (abs_y - x); \
		angle = coeff_2 - coeff_1 * r; \
	} \
	return y < 0.0 ? -angle : angle; \
}";
		}

		sEigenProgram +=
"\
/* Beware: C atan2(0,0) is defined, but fragment shaders return NaN. */ \
float safe_atan2(float b, float a) \
{ \
	if(abs(b) < 0.0001 && abs(a) < 0.0001) return 0.0; \
	return atan2(b, a); \
} \
void symmetric_eigen(vec4 tensor, out vec2 val, out vec4 vec) \
{ \
	float a = tensor[0], b = tensor[1], c = tensor[2], d = tensor[3], e = a+d; \
	float f = e*e-4.0*(a*d-b*c); \
	f = sqrt(f); \
	float l1 = 0.5*(e-f), l2 = 0.5*(e+f); \
	float theta1 = safe_atan2(l2-a,b), theta2 = safe_atan2(l1-a,b); \
	val = vec2(l2, l1); \
	vec = vec4(cos(theta1), sin(theta1), cos(theta2), sin(theta2)); \
} \
\
void main(void) \
{\
	vec4 tensor = texture2DRect(G, gl_TexCoord[0].xy); \
	tensor.xyzw = tensor.xyyz; \
	vec2 val; \
	vec4 vec; \
	symmetric_eigen(tensor, val, vec); \
	float l1 = val.y; \
	float l2 = val.x; \
	float n1 = pow(1.0+l1+l2,-power1); \
	float n2 = pow(1.0+l1+l2,-power2); \
	float ux = vec.z, uy = vec.w; \
	float vx = vec.x, vy = vec.y; \
\
	gl_FragColor = vec4( \
		n1*ux*ux + n2*vx*vx, \
		n1*ux*uy + n2*vx*vy, \
		n1*uy*uy + n2*vy*vy, \
		0.0); \
}";
		EigenShader.m_iHandle = gpu.CompileShader("eigenvalue shader", sEigenProgram);
	}

	if(PrepShader.m_iHandle == 0)
	{
		string sPrepProgram = 
"#extension GL_ARB_texture_rectangle : require\n" \
"uniform sampler2DRect source; \
uniform float vx, vy; \
uniform float dl; \
void main(void) \
{\
	vec4 s = texture2DRect(source, gl_TexCoord[0].xy); \
	float u = s.x*vx + s.y*vy;		/*  _  */ \
	float v = s.y*vx + s.z*vy;		/*   |  */ \
	float n = sqrt(u*u+v*v) + (1e-5);	/*  /   */ \
	float dln = dl/n; \
	gl_FragColor = vec4(n, u*dln, v*dln, 0); \
}";
		PrepShader.m_iHandle = gpu.CompileShader("prep shader", sPrepProgram);
	}

	if(MainShader.m_iHandle == 0)
	{
		string sMainProgram = 
"#extension GL_ARB_texture_rectangle : require\n" \
"uniform sampler2DRect W; \
uniform sampler2DRect source_image; \
uniform float sqrt2amplitude; \
uniform float dl; \
uniform float gauss_prec; \
uniform bool fast_approx; \
uniform bool alt_amplitude; \
uniform vec2 dimensions; \
void main(void) \
{\
	vec2 source_coordinate = gl_TexCoord[0].xy; \
\
	/*source_coordinate.xy = vec2(408, 108.73);*/ \
	\
	float n = texture2DRect(W, source_coordinate).x; \
\
	/* Big WTF: n is occasionally coming through as a very large value (> 25), \
	 * resulting in extremely high lengths, which makes this loop take a long time \
	 * to complete (so long that the mouse starts to freeze and then the driver aborts \
	 * the fragment).  However, these values don't exist in the texture, and if we check \
	 * for them and output a sentinel color, the color never shows up.  They happen even \
	 * if source_coordinate is somewhere in the middle of the image.  This doesn't make \
	 * sense at all. */ \
	 n = min(n, 2.0); \
\
/*		gl_FragColor = vec4(n,n,n,n);\
		return;\
		if(n > 20.0) \
		{ \
*/ /*		gl_FragColor = n; */\
/*		gl_FragColor = vec4(1,0,1,1);*/ \
/*		n = 20; *//*gl_FragColor = vec4(1,0,1,1); */\
/*		return; \
		} \
*/\
	float fsigma = n * sqrt2amplitude; \
	float length = gauss_prec * fsigma; \
	float S = 0.0; \
	float l = 0.0; \
	vec4 puv = vec4(0,0,0,0); \
	vec4 tot = vec4(0,0,0,0); \
	\
	int count = 0; \
	while(l <= length && count < 32) \
	{ \
		++count; \
		float coef; \
	\
		if(fast_approx) \
		{ \
			coef = 1.0; \
		} else { \
			float fsigma2 = 2.0*fsigma*fsigma; \
			float fsigma2r = 1.0/fsigma2; \
			coef = exp(-l*l*fsigma2r); \
		} \
\
		/* This bounds check matches the one in do_blur_anisotropic_with_vectors_angle.  Note \
		 * the odd rounding: we stop mixing if our source coordinate is beyond the center of \
		 * the last pixel, even though that would land within the pixel and not outside the image. \
		 * I'm not sure why this is necessary, but it prevents border artifacts. */ \
		if(source_coordinate.x < 0.0 || source_coordinate.y < 0.0 || source_coordinate.x + 0.5 > dimensions.x || source_coordinate.y + 0.5 > dimensions.y) \
			break; \
\
		vec4 c = texture2DRect(source_image, source_coordinate); \
		c *= coef; /* no-op in fast approx; x will always be 1 */ \
		tot += c; \
\
		S += coef; \
\
		/* Iterate source_coordinate. */ \
		vec4 vcW = texture2DRect(W, source_coordinate); \
		if(puv.y*vcW.y + puv.z*vcW.z < 0.0) \
			vcW.yz *= -1.0; \
\
		if(alt_amplitude) \
		{ \
			float n2 = vcW.x; \
			l += dl * (n/n2); \
		} \
		else \
			l += dl; \
		source_coordinate.x += vcW.y; \
		source_coordinate.y += vcW.z; \
		puv = vcW; \
	} \
	gl_FragColor = tot / S; \
}";

		MainShader.m_iHandle = gpu.CompileShader("main shader", sMainProgram);
	}

	/* We should be able to simply use glColor4f(1.0/N, 1.0/N, 1.0/N, 1.0/N), and
	 * that works on a Geforce 9600, but on a 6600, the color inputs are being
	 * clamped to 0,1 before the color multiply for some reason.  The 9600 behavior
	 * seems to spec, and I can't find any documentation explaining the 6600's, but
	 * it works if we do it manually in a shader. */
	if(MultShader.m_iHandle == 0)
	{
		string sMultProgram = 
"#extension GL_ARB_texture_rectangle : require\n" \
"uniform sampler2DRect source; \
uniform float c; \
void main(void) \
{\
	vec4 s = texture2DRect(source, gl_TexCoord[0].xy); \
	gl_FragColor = s * c; \
}";
		MultShader.m_iHandle = gpu.CompileShader("mult shader", sMultProgram);
	}
}


void GreycGPU::Cleanup()
{
	if(!gpu.Initialized())
		return;

	IIRShader.Destroy();
	EigenShader.Destroy();
	PrepShader.Destroy();
	MainShader.Destroy();
	MultShader.Destroy();
	gpu.Destroy();
}

void GreycGPU::gpu_blur_anisotropic_main(const GPUTexture &SourceImage, const CImgF &G,
		volatile bool *pStopRequest, volatile LONG *pProgress,
		int iWidth,
		int iHeight,
		const bool alt_amplitude,
		const float amplitude,
		const float da,
		const float dl,
		const float gauss_prec,
		const float sharpness,
		const float anisotropy,
		const unsigned int interpolation,
		const bool fast_approx, bool bHighBitDepth)
{
	InitShaders();

	double tt = gettime();
	glEnable(GL_TEXTURE_RECTANGLE_ARB);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, 1, 0, 1, -1, 1);
	glViewport(0, 0, iWidth, iHeight);
	glClearColor(0,0,0,0);

	const GLenum GL_FORMAT_G = GL_RGBA16F_ARB; /* must be a float format */
	const GLenum GL_FORMAT_W = GL_RGBA16F_ARB; /* must be a float format */
	const GLenum GL_FORMAT_SUM = bHighBitDepth? GL_RGBA32F_ARB:GL_RGBA16F_ARB; /* must be a float format */
//	GPUDeriche(SourceImage, iWidth, iHeight, this->GetSettings().alpha, GL_RGBA32F_ARB);
//return;


	/* Convert G to a texture. */
	GPUTexture GTexture;
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, GTexture.m_iHandle);
	GPUTexture::SetDefaults(interpolation == 0);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, G.stride / G.dim);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_FORMAT_G, G.width, G.height, 0, GL_RGBA, GL_FLOAT, G.data);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

	printf("Timing (GPU): texture load %f\n", gettime() - tt);
	tt = gettime();

	{
		GPUTexture G2Texture;
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB, G2Texture.m_iHandle);
                glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_FORMAT_G, G.width, G.height, 0, GL_RGB, GL_FLOAT, NULL);
		GPUTexture::SetDefaults(interpolation == 0);

		GPUFrameBuffer G2FrameBuffer;
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, G2FrameBuffer.m_iHandle);
		glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_RECTANGLE_ARB, G2Texture.m_iHandle, 0);
		GPUFrameBuffer::Check("G2FrameBuffer");

	        glUseProgramObjectARB(EigenShader.m_iHandle);

		const float nsharpness = max(sharpness,1e-5f);
		const float power1 = 0.5f*nsharpness;
		const float power2 = power1/(1e-7f+1.0f-anisotropy);
		GL::glSetUniform1f(EigenShader.m_iHandle, "power1", power1);
		GL::glSetUniform1f(EigenShader.m_iHandle, "power2", power2);
		GL::glSetUniform1i(EigenShader.m_iHandle, "G", 0);
		GL::glSetUniform1i(EigenShader.m_iHandle, "atan2_texture", 1);

		// GL::glBindTextures(GTexture.m_iHandle, m_pATan2Texture->m_iHandle);
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB, GTexture.m_iHandle);

		gpu.Draw(iWidth, iHeight);

		/* We're done with GTexture. */
		swap(GTexture.m_iHandle, G2Texture.m_iHandle);
	}
	printf("Timing (GPU): G2 %f\n", gettime() - tt);
	tt = gettime();

	/* Each OutputTexture is divided by the resulting source_coordinate_map.z; the
	 * results are summed here: */
	GPUTexture SumTexture;
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, SumTexture.m_iHandle);
	GPUTexture::SetDefaults(interpolation == 0);
	glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_FORMAT_SUM, iWidth, iHeight, 0, GL_RGBA, GL_FLOAT, NULL);

	GPUFrameBuffer SumFrameBuffer;
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, SumFrameBuffer.m_iHandle);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_RECTANGLE_ARB, SumTexture.m_iHandle, 0);
	GPUFrameBuffer::Check("SumFrameBuffer");

	glClear(GL_COLOR_BUFFER_BIT);

	GPUTexture WTexture;
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, WTexture.m_iHandle);
	GPUTexture::SetDefaults(interpolation == 0);
	glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_FORMAT_W, iWidth, iHeight, 0, GL_RGB, GL_FLOAT, NULL);

	GPUFrameBuffer WFrameBuffer;
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, WFrameBuffer.m_iHandle);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_RECTANGLE_ARB, WTexture.m_iHandle, 0);
	GPUFrameBuffer::Check("WFrameBuffer");
	glClear(GL_COLOR_BUFFER_BIT);

	glFinish();
	printf("Timing (GPU): setup %f\n", gettime() - tt);
	tt = gettime();

	/* Define inputs for the main shader. */
	glUseProgramObjectARB(MainShader.m_iHandle);
	GL::glSetUniform1f(MainShader.m_iHandle, "sqrt2amplitude", sqrtf(2*amplitude));
	GL::glSetUniform1f(MainShader.m_iHandle, "dl", dl);
	GL::glSetUniform1f(MainShader.m_iHandle, "gauss_prec", gauss_prec);
	GL::glSetUniform1i(MainShader.m_iHandle, "fast_approx", fast_approx);
	GL::glSetUniform1i(MainShader.m_iHandle, "alt_amplitude", alt_amplitude);
	GL::glSetUniform2f(MainShader.m_iHandle, "dimensions", iWidth, iHeight);

	GL::glSetUniform1i(MainShader.m_iHandle, "source_image", 0);
	GL::glSetUniform1i(MainShader.m_iHandle, "W", 1);
	
	int N = 0;
	for(float theta=(360%(int)da)/2.0f; theta<360; theta += da)
	{
		++N;

		/* PrepShader converts GTexture to WTexture. */
		{
			glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, WFrameBuffer.m_iHandle);
		        glUseProgramObjectARB(PrepShader.m_iHandle);

			GL::glSetUniform1i(PrepShader.m_iHandle, "source", 0);
			const float thetar = (float)(theta*M_PI/180);
			GL::glSetUniform1f(PrepShader.m_iHandle, "vx", cosf(thetar));
			GL::glSetUniform1f(PrepShader.m_iHandle, "vy", sinf(thetar));
			GL::glSetUniform1f(PrepShader.m_iHandle, "dl", dl);
			GL::glBindTextures(GTexture.m_iHandle);

			gpu.Draw(iWidth, iHeight);
		}
//			printf("------> p1 %f\n", gettime() - tt);
//			tt = gettime();

		if(0)
			GPUTexture::ExamineTexture(WTexture.m_iHandle);

//			printf("------> maps %f\n", gettime() - tt);
//			tt = gettime();
		glFinish();
//	printf("Timing (GPU): prep %f\n", gettime() - tt); tt = gettime();

		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, SumFrameBuffer.m_iHandle);
		glUseProgramObjectARB(MainShader.m_iHandle);

//			printf("------> ... setup %f\n", gettime() - tt);
//			tt = gettime();

		/* Run the main filter. */
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE);

		GL::glBindTextures(SourceImage.m_iHandle, WTexture.m_iHandle);

		gpu.Draw(iWidth, iHeight);

		if(0)
			GPUTexture::ExamineTexture(SumTexture.m_iHandle);
		glBlendFunc(GL_ONE, GL_ZERO);
		glDisable(GL_BLEND);
		glFinish();

		progress_and_check_cancel;

//			printf("------> p2 %f\n", gettime() - tt);
//			tt = gettime();
//		GLenum error = glGetError(); if(error) printf("xxxxx error %i\n", error);
	}
	printf("Timing (GPU): main %f\n", gettime() - tt); tt = gettime();

	/* We summed up N passes into SumTexture.  Divide by N to get the average.  This
	 * is our final pass, so output into our output format. */
	GPUFrameBuffer OutputFrameBuffer;
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OutputFrameBuffer.m_iHandle);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_RECTANGLE_ARB, SourceImage.m_iHandle, 0);
	GPUFrameBuffer::Check("OutputFrameBuffer");

	glUseProgramObjectARB(MultShader.m_iHandle);
	GL::glSetUniform1f(MultShader.m_iHandle, "c", 1.0 / N);
	GL::glSetUniform1i(MultShader.m_iHandle, "source", 0);
	GL::glBindTextures(SumTexture.m_iHandle);
	
	gpu.Draw(iWidth, iHeight);

	glUseProgramObjectARB(0);

	if(0)
	{
		GPUTexture::ExamineTexture(SumTexture.m_iHandle);
		GPUTexture::ExamineTexture(SourceImage.m_iHandle);
	}

	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
	glFinish();
	printf("Timing (GPU): finalize %f\n", gettime() - tt); tt = gettime();
	GLenum e = glGetError(); if(e) printf("OpenGL error 5 %i\n", e);
}

namespace
{
	void GetFormatForComponents(int i, GLenum &format, GLenum &internalformat, GLenum &type, bool bHighBitDepth)
	{
		if(!bHighBitDepth)
		{
			type = GL_FLOAT;
			switch(i)
			{
			case 1: format = GL_LUMINANCE; internalformat = GL_LUMINANCE16F_ARB; return;
			case 2: format = GL_LUMINANCE_ALPHA; internalformat = GL_LUMINANCE_ALPHA16F_ARB; return;
			case 3: format = GL_RGB; internalformat = GL_RGB16F_ARB; return;
			case 4: format = GL_RGBA; internalformat = GL_RGBA16F_ARB; return;
			}
		}
		else
		{
			type = GL_FLOAT;
			switch(i)
			{
			case 1: format = GL_LUMINANCE; internalformat = GL_LUMINANCE32F_ARB; return;
			case 2: format = GL_LUMINANCE_ALPHA; internalformat = GL_LUMINANCE_ALPHA32F_ARB; return;
			case 3: format = GL_RGB; internalformat = GL_RGB32F_ARB; return;
			case 4: format = GL_RGBA; internalformat = GL_RGBA32F_ARB; return;
			}
		}
	}
}

void GreycGPU::ProcessAnisotropic(CImgF &img, const CImgF &G,
		volatile bool *pStopRequest, volatile LONG *pProgress,
		const bool alt_amplitude,
		const float amplitude,
		const float da,
		const float dl,
		const float gauss_prec,
		const float sharpness,
		const float anisotropy,
		const unsigned int interpolation,
		const bool fast_approx,
		const bool bHighBitDepth)
{
	double tt = gettime();
	gpu.Init();
	printf("Timing (GPU): init %f\n", gettime() - tt); tt = gettime();

	GPUTexture SourceImage;
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, SourceImage.m_iHandle);
	GPUTexture::SetDefaults(interpolation == 0);

	GLenum format, internalformat, type;
	GetFormatForComponents(img.dim, format, internalformat, type, bHighBitDepth);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, img.stride / img.dim);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, internalformat, img.width, img.height, 0, format, type, img.data);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

	gpu_blur_anisotropic_main(SourceImage, G,
		pStopRequest, pProgress, img.width, img.height, alt_amplitude, amplitude, da,
		dl, gauss_prec, sharpness, anisotropy, interpolation, fast_approx, bHighBitDepth);

	tt = gettime();
	/* Copy the final image out into img. */
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, SourceImage.m_iHandle);

	{
		int iHeight, iWidth;
		glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB, 0, GL_TEXTURE_HEIGHT, &iHeight);
		glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB, 0, GL_TEXTURE_WIDTH, &iWidth);
		printf("Copying out (%ix%i) -> (%ix%i)\n", iHeight, iWidth, img.width, img.height);
		assert(iHeight == img.height);
		assert(iWidth == img.width);
	}

	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glPixelStorei(GL_PACK_ROW_LENGTH, img.stride / img.dim);
	glGetTexImage(GL_TEXTURE_RECTANGLE_ARB, 0, format, type, img.data);
	glPixelStorei(GL_PACK_ROW_LENGTH, 0);
	GLenum error = glGetError(); if(error) printf("OpenGL error %i\n", error);

	printf("Timing (GPU): finish %f\n", gettime() - tt);
}

/*
 * This implements DericheBlur in a shader.  It's not useful: the IIR feedback prevents using
 * the GPU effectively, and the result is about 50% slower than the plain C Deriche implementation,
 * and much slower than the SSE version.  I've kept it around in case I can figure out how to
 * unroll it, or to approximate the IIR with an FIR...
 */
#if 0
void GreycGPU::GPUDeriche(const GPUTexture &image, int iWidth, int iHeight, float sigma, int iInternalFormat)
{
	double tt = gettime();
	/* Create two ping-pong textures. */
	GPUTexture Tex1;
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, Tex1.m_iHandle);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, iInternalFormat, iWidth, iHeight, 0, GL_RGBA, GL_FLOAT, NULL);
	GPUTexture::SetDefaults(true);

	GPUFrameBuffer Tex1Buffer;
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, Tex1Buffer.m_iHandle);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_RECTANGLE_ARB, Tex1.m_iHandle, 0);
	GPUFrameBuffer::Check();

	GPUTexture Tex2;
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, Tex2.m_iHandle);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, iInternalFormat, iWidth, iHeight, 0, GL_RGBA, GL_FLOAT, NULL);
	GPUTexture::SetDefaults(true);

	GPUFrameBuffer Tex2Buffer;
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, Tex2Buffer.m_iHandle);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_RECTANGLE_ARB, Tex2.m_iHandle, 0);
	GPUFrameBuffer::Check();
	GLenum error = glGetError(); if(error) printf("xxx1 error %i\n", error);

	const float nsigma = sigma<0.1f?0.1f:sigma;
	const float alpha = 1.695f/nsigma;
	const float ema = expf(-alpha);
	const float ema2 = expf(-2*alpha);
	const float b1 = -2*ema;
	const float b2 = ema2;
	const float k = (1-ema)*(1-ema)/(1+2*alpha*ema-ema2);
	const float a0 = k;
	const float a1 = k*(alpha-1)*ema;
	const float a2 = k*(alpha+1)*ema;
	const float a3 = -k*ema2;

        glUseProgramObjectARB(IIRShader.m_iHandle);
	GL::glSetUniform4f(IIRShader.m_iHandle, "w", a0, a1, b1, b2);
	GL::glSetUniform4f(IIRShader.m_iHandle, "dir", -1.0f, 0, -2.0f, 0);
//		const float yc = a2*xn + a3*xa - b1*yn - b2*ya;
//		GL::glSetUniform4f(IIRShader.m_iHandle, "w", a2, a3, b1, b2);
	GL::glSetUniform1i(IIRShader.m_iHandle, "img", 0);
	GL::glSetUniform1i(IIRShader.m_iHandle, "prev", 1);

	// XXX: initialize edges from coefp and coefn
	{
		const float coefp = (a0+a1)/(1+b1+b2);
		const float coefn = (a2+a3)/(1+b1+b2);
	        glUseProgramObjectARB(0);
		GL::glBindTextures(image.m_iHandle);
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, Tex1Buffer.m_iHandle);
		gpu.Draw(iWidth, iHeight);
	}

	for(int x = 0; x < iWidth; ++x)
	{
	        glUseProgramObjectARB(IIRShader.m_iHandle);

		GL::glBindTextures(image.m_iHandle, Tex1.m_iHandle);
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, Tex2Buffer.m_iHandle);

		glBegin(GL_QUADS);
		glTexCoord2f(x+0.25,	0);		glVertex2f((x+0.25) / float(iWidth),0);
		glTexCoord2f(x+0.75,	0);		glVertex2f((x+0.75) / float(iWidth),0);
		glTexCoord2f(x+0.75,	iHeight);	glVertex2f((x+0.75) / float(iWidth),1);
		glTexCoord2f(x+0.25,	iHeight);	glVertex2f((x+0.25) / float(iWidth),1);
		glEnd();

		/* Copy the column we just output back to Tex1. */
		// XXX: since we're dealing with just one column/row at a time, there's no
		// reason to allocate Tex2 full size
		GL::glBindTextures(Tex2.m_iHandle);
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, Tex1Buffer.m_iHandle);
	        glUseProgramObjectARB(0);

		glBegin(GL_QUADS);
		glTexCoord2f(x+0.25,	0);		glVertex2f((x+0.25) / float(iWidth),0);
		glTexCoord2f(x+0.75,	0);		glVertex2f((x+0.75) / float(iWidth),0);
		glTexCoord2f(x+0.75,	iHeight);	glVertex2f((x+0.75) / float(iWidth),1);
		glTexCoord2f(x+0.25,	iHeight);	glVertex2f((x+0.25) / float(iWidth),1);
		glEnd();
	}
#if 1
	for(int x = 0; x < iHeight; ++x)
	{
	        glUseProgramObjectARB(IIRShader.m_iHandle);

		GL::glBindTextures(image.m_iHandle, Tex1.m_iHandle);
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, Tex2Buffer.m_iHandle);

		glBegin(GL_QUADS);
		glTexCoord2f(0,x+0.25);		glVertex2f(0,(x+0.25) / float(iHeight));
		glTexCoord2f(0,x+0.75);		glVertex2f(0,(x+0.75) / float(iHeight));
		glTexCoord2f(iWidth,x+0.75);	glVertex2f(1,(x+0.75) / float(iHeight));
		glTexCoord2f(iWidth,x+0.25);	glVertex2f(1,(x+0.25) / float(iHeight));
		glEnd();

		/* Copy the column we just output back to Tex1. */
		// XXX: since we're dealing with just one column/row at a time, there's no
		// reason to allocate Tex2 full size
		GL::glBindTextures(Tex2.m_iHandle);
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, Tex1Buffer.m_iHandle);
	        glUseProgramObjectARB(0);

		glBegin(GL_QUADS);
		glTexCoord2f(0,x);		glVertex2f(0,x / float(iHeight));
		glTexCoord2f(0,x+1);		glVertex2f(0,(x+1) / float(iHeight));
		glTexCoord2f(iWidth,x+1);	glVertex2f(1,(x+1) / float(iHeight));
		glTexCoord2f(iWidth,x);	glVertex2f(1,x / float(iHeight));
		glEnd();
	}
#endif

	glFinish();
	error = glGetError(); if(error) printf("xxx2 error %i\n", error);

	GPUFrameBuffer ImgBuffer;
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, ImgBuffer.m_iHandle);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_RECTANGLE_ARB, image.m_iHandle, 0);
	GPUFrameBuffer::Check();

	glUseProgramObjectARB(0);
	GL::glBindTextures(Tex2.m_iHandle);
	gpu.Draw(iWidth, iHeight);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
	error = glGetError(); if(error) printf("xxx3 error %i\n", error);
	printf("------> blur load %f\n", gettime() - tt);
}
#endif
