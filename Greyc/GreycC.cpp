/*
 * Implement the core algorithm; CPU version.  Parts of this are also used in
 * GPU mode.
 *
 * License information is at the bottom of the file.
 */

#include "GreycC.h"
#include "DericheBlur.h"
#include "GaussianBlur.h"
#include <math.h>

void CImg_get_tensor_at(const CImgF &img, float *pOut, const unsigned int x=0, const unsigned int y=0, const unsigned int z=0)
{
    const float *pData = img.ptr(x,y,0);
    pOut[0] = pData[0];
    pOut[1] = pData[1];
    pOut[2] = pData[1];
    pOut[3] = pData[2];
}

// Return the 2D structure tensor field of an image
// This avoids returning the object; that causes a copy to be made, which raises our peak
// memory usage significantly.
void get_structure_tensorXY(const CImgF &img, CImgF &res)
{
	if(img.is_empty())
	{
		res = CImgF();
		return;
	}
	/* We allocate 4 components even though we only use 3, for SSE and OpenGL. */
	res.alloc(img.width,img.height,4);
	res.fill(0);
	CImgI_3x3(I);

	// Precise forward/backward finite differences
	cimgI_forV(img,k)
	{
		cimgI_for3x3(img,x,y,k,I)
		{
			const float
				ixf = Inc-Icc, ixb = Icc-Ipc,
				iyf = Icn-Icc, iyb = Icc-Icp;
			res(x,y,0) += 0.5f*(ixf*ixf+ixb*ixb);
			res(x,y,1) += 0.25f*(ixf*iyf+ixf*iyb+ixb*iyf+ixb*iyb);
			res(x,y,2) += 0.5f*(iyf*iyf+iyb*iyb);
		}
	}
}

    //! Blur an image in an anisotropic way.
    /**
       \param mask Binary mask.
       \param amplitude Amplitude of the anisotropic blur.
       \param sharpness Contour preservation.
       \param anisotropy Smoothing anisotropy.
       \param alpha Image pre-blurring (gaussian).
       \param sigma Regularity of the tensor-valued geometry.
       \param dl Spatial discretization.
       \param da Angular discretization.
       \param gauss_prec Precision of the gaussian function.
       \param interpolation Used interpolation scheme (0 = nearest-neighbor, 1 = linear, 2 = Runge-Kutta)
       \param fast_approx Tell to use the fast approximation or not
       \param geom_factor Geometry factor.
       \param stage Processing stage to finish at:
          0 = do all
	  1 = return first-stage blurred image
	  2 = return scaled or normalized image
	  3 = return structure tensors
	  4 = return blurred structure tensors

    **/
void do_blur_anisotropic_prep(CImgF &img, CImgF &G, volatile bool *pStopRequest, volatile LONG *pProgress,
                        float fPreBlur, float alpha, float sigma, float geom_factor, int stage)
{
	if (img.is_empty())
		return;

	alpha = max(alpha, 0.0f);
	sigma = max(sigma, 0.0f);

	if(fPreBlur > 0)
	{
	double f = gettime();
		gaussian_blur_estimation(img, fPreBlur, pStopRequest);
	printf("Timing (prep): gaussian took %f\n", gettime() - f); f = gettime();
	}
	if(stage == 1)
		return;
	check_cancel;

	CImgF blurred(img);
double f = gettime();
	deriche(blurred, alpha);
	printf("Timing (prep): alpha blur %f\n", gettime() - f); f = gettime();

	if(stage == 2)
	{
		img.assign(blurred);
		return;
	}

	check_cancel;

	if (geom_factor>0) blurred.scale(geom_factor);
	else blurred.normalize(0, -geom_factor);
printf("Timing (prep): geom_factor %f\n", gettime() - f); f = gettime();

	if(stage == 3)
	{
		img.assign(blurred);
		return;
	}

	check_cancel;

	get_structure_tensorXY(blurred, G);
printf("Timing (prep): get_structure_tensorXY %f\n", gettime() - f); f = gettime();
	if(stage == 4)
	{
		G.scale(0.05f);
		img.assign(G);
		return;
	}

	check_cancel;

	if (sigma>0)
		deriche(G, sigma);
printf("Timing (prep): sigma %f\n", gettime() - f); f = gettime();

	check_cancel;

	if(stage == 5)
	{
		G.scale(1/10000.0f);
		img.assign(G);
		return;
	}
}

//! Compute the eigenvalues and eigenvectors of a symmetric matrix.
void symmetric_eigen(float tensor[4], float val[2], float vec[4])
{
	const float a = tensor[0], b = tensor[1], c = tensor[2], d = tensor[3], e = a+d;
	float f = e*e-4*(a*d-b*c);
	f = sqrtf(f);
	const float l1 = 0.5f*(e-f), l2 = 0.5f*(e+f);
	const float theta1 = atan2f(l2-a,b), theta2 = atan2f(l1-a,b);
	val[0] = (float) l2;
	val[1] = (float) l1;
	vec[0] = cosf(theta1);
	vec[1] = sinf(theta1);
	vec[2] = cosf(theta2);
	vec[3] = sinf(theta2);
}

void do_blur_anisotropic(const CImgF &G, CImgF &G2, volatile bool *pStopRequest, volatile LONG *pProgress,
			Slices *pSlices, float sharpness, float anisotropy)
{
	sharpness = max(sharpness, 0.0f);
	anisotropy = clamp(anisotropy, 0.0f, 1.0f);

	const float nsharpness = max(sharpness,1e-5f);
	const float power1 = 0.5f*nsharpness;
	const float power2 = power1/(1e-7f+1.0f-anisotropy);
	float val[2];
	float vec[4];
	float tensor[4];

	int y;
	while(pSlices->Get(y))
	{
		progress_and_check_cancel;
		cimgI_forX(G,x)
		{
			CImg_get_tensor_at(G, tensor, x, y);
			symmetric_eigen(tensor, val, vec);
			const float l1 = val[1];
			const float l2 = val[0];
			const float n1 = powf(1.0f+l1+l2,-power1);
			const float n2 = powf(1.0f+l1+l2,-power2);
			const float ux = vec[2], uy = vec[3];
			const float vx = vec[0], vy = vec[1];
			G2(x,y,0) = n1*ux*ux + n2*vx*vx;
			G2(x,y,1) = n1*ux*uy + n2*vx*vy;
			G2(x,y,2) = n1*uy*uy + n2*vy*vy;
		}
	}
}


/* If W.dim is 3, we store all of the u, v, n values for each point.  If W.dim is 1, then
 * we only store the n value; it needs a sqrt to compute, so it's the expensive one and the
 * others will be calculated on the fly to save memory. */
void do_blur_anisotropic_init_for_angle(const CImgF &G, CImgF &W, volatile bool *pStopRequest, volatile LONG *pProgress,
			Slices *pSlices, float theta, const float dl)
{
	const float thetar = (float)(theta*M_PI/180);
	const float vx = cosf(thetar);
	const float vy = sinf(thetar);

	int y;
	while(pSlices->Get(y))
	{
		progress_and_check_cancel;

		const float *pa = G.ptr(0,y,0);
		float *pd0 = W.ptr(0,y,0);

		cimgI_forX(G,x)
		{
			/* These represent distances in the image.  Store these as 8-bit fixed-point to save
			 * space and improve caching.  Use a shift of 4 to support values between -31 and 31. */
			const float a = *(pa++), b = *(pa++), c = *(pa++); pa++; // 4 components
			const float u = (float)(a*vx + b*vy);		/*   _  */
			const float v = (float)(b*vx + c*vy);		/*  |  */
			const float n = sqrtf(u*u+v*v) + 1e-5f;		/*   /  */
			const float dln = dl/n;
			*(pd0++) = n;
			*(pd0++) = u*dln;
			*(pd0++) = v*dln;
			pd0++; // 4 components
		}
	}
}

    //! Get a blurred version of an image following a field of diffusion tensors.
    /**
       \param G = Field of square roots of diffusion tensors used to drive the smoothing.
       \param amplitude = amplitude of the smoothing.
       \param dl = spatial discretization.
       \param da = angular discretization.
       \param gauss_prec = precision of the gaussian function.
       \param interpolation Used interpolation scheme (0 = nearest-neighbor, 1 = linear, 2 = Runge-Kutta)
       \param fast_approx = Tell to use the fast approximation or not.
    **/

/*
 * alt_amplitude: We're following a curve; the distance we go from x,y is defined by n = [x,y,2].
 * If the amplitude is high, then in areas not near any distinct contour, [x,y,2] may be much higher
 * than pixels near the contour; high enough that we can follow that distance and end up near a contour,
 * and have more distance left to travel than we would have if we had started at the pixel near the
 * contour, causing us to travel too far.  This causes the ghosting effects, where color far to the right
 * of a line will appear far to the left, even though the area closer to the edge itself is perfectly
 * clear.
 *
 * alt_amplitude causes us to take the [x,y,2] value of each pixel we passing through into consideration,
 * instead of just the one we start at.
 *
 * If we start at a pixel with n = 5 (travel 5 pixels), and the next pixel we hit is n2 = 3 (only travel
 * 3 pixels), then we don't want to go as far, just as we wouldn't had we started on the n2 = 3 pixel.
 * Treat the distance travelled as n/n2: the second pixel will count as 5/3 as many pixels (as if we'd
 * traversed more than we have).
 *
 * If we're following a clear contour, and each n along the line we're following is 5, then the distance
 * each pixel is defining is similar, and this will have no effect.
 *
 * This eliminates ghosting effects at higher amplitudes.
 */
void do_blur_anisotropic_with_vectors_angle(CImgF &img, CImgF &W, const CImg &mask,  CImgF &dest,
			volatile bool *pStopRequest, volatile LONG *pProgress,
			Slices *pSlices,
			const bool alt_amplitude,
			const float amplitude,
			const float dl,
			const float gauss_prec, const unsigned int interpolation,
			const bool fast_approx)
{
        const float sqrt2amplitude = sqrtf(2*amplitude);
        const int dx1 = img.width-1;
        const int dy1 = img.height-1;
        CImgF tmp; tmp.alloc(img.dim, 1, 1);
	const bool no_mask = mask.Empty();

	int y;
	while(pSlices->Get(y))
	{
		progress_and_check_cancel;
		cimgI_forX(img,x)
		{
			if(!no_mask && !mask(x,y))
				continue;

			tmp.fill(0);
			const float n = (float)W(x,y,0);
			const float fsigma = n * sqrt2amplitude;
			const float length = gauss_prec * fsigma;
			const float fsigma2 = 2*fsigma*fsigma;			/* only used when !fast_approx */
			const float fsigma2r = 1/fsigma2;			/* only used when !fast_approx */
			float S = 0;
			float pu = 0, pv = 0;
			float X = (float)x;
			float Y = (float)y;

			switch (interpolation)
			{
			case 0: {
				// Nearest-neighbor interpolation for 2D images
				for (float l=0; l<length && X>=0 && X<=dx1 && Y>=0 && Y<=dy1; )
				{
					const int cx = lrintf(X);
					const int cy = lrintf(Y);
					if(fast_approx)
					{
						cimgI_forV(img,k)
							tmp(k,0,0) += (float) img(cx,cy,k);
						++S;
					}
					else
					{
						const float coef = expf(-l*l*fsigma2r);
						cimgI_forV(img,k)
							tmp(k,0,0) += coef * img(cx,cy,k);
						S += coef;
					}

					float u = W(cx,cy,1);
					float v = W(cx,cy,2);

					if(alt_amplitude)
					{
						const float n2 = (float)W(cx,cy,0);
						l += dl * (n/n2);
					}
					else
						l += dl;

					if(pu*u + pv*v<0) { u=-u; v=-v; }
					X += u; Y += v;
					pu=u; pv=v;
				}
				} break;

#undef cimg_valign2d
#define cimg_valign2d(i,j) \
{ float &u = W(i,j,1), &v = W(i,j,2); \
if (u*curru + v*currv<0) { u=-u; v=-v; }}

			case 1: {
				// Linear interpolation for 2D images
				for (float l=0; l<length && X>=0 && X<=dx1 && Y>=0 && Y<=dy1; )
				{
					const int cx = (int)X, px = (cx-1<0)?0:cx-1, nx = (cx+1>dx1)?dx1:cx+1;
					const int cy = (int)Y, py = (cy-1<0)?0:cy-1, ny = (cy+1>dy1)?dy1:cy+1;
					const float curru = (float)W(cx,cy,1);
					const float currv = (float)W(cx,cy,2);
					cimg_valign2d(px,py); cimg_valign2d(cx,py); cimg_valign2d(nx,py);
					cimg_valign2d(px,cy);                       cimg_valign2d(nx,cy);
					cimg_valign2d(px,ny); cimg_valign2d(cx,ny); cimg_valign2d(nx,ny);
					float u = (float)(W.linear_pix2d(X,Y,1));
					float v = (float)(W.linear_pix2d(X,Y,2));
					if ((pu*u + pv*v)<0) { u=-u; v=-v; }
					if (fast_approx) { cimgI_forV(img,k) tmp(k,0,0)+=(float)img.linear_pix2d(X,Y,k); ++S; }
					else
					{
						const float coef = expf(-l*l/fsigma2);
						cimgI_forV(img,k)
							tmp(k,0,0)+=coef*img.linear_pix2d(X,Y,k);
						S+=coef;
					}
					X+=(pu=u); Y+=(pv=v);

					if(alt_amplitude)
					{
						const float n2 = (float)W(cx,cy,0);
						l += dl * (n/n2);
					}
					else
						l += dl;
				}
			} break;

			default: {
				// 2nd-order Runge-kutta interpolation for 2D images
				for (float l=0; l<length && X>=0 && X<=dx1 && Y>=0 && Y<=dy1; )
				{
					const int cx = (int)X, px = (cx-1<0)?0:cx-1, nx = (cx+1>dx1)?dx1:cx+1;
					const int cy = (int)Y, py = (cy-1<0)?0:cy-1, ny = (cy+1>dy1)?dy1:cy+1;
					const float curru = (float)W(cx,cy,1);
					const float currv = (float)W(cx,cy,2);
					cimg_valign2d(px,py); cimg_valign2d(cx,py); cimg_valign2d(nx,py);
					cimg_valign2d(px,cy);                       cimg_valign2d(nx,cy);
					cimg_valign2d(px,ny); cimg_valign2d(cx,ny); cimg_valign2d(nx,ny);
					const float u0 = (float)(0.5f*W.linear_pix2d(X,Y,1));
					const float v0 = (float)(0.5f*W.linear_pix2d(X,Y,2));
					float u = (float)(W.linear_pix2d(X+u0,Y+v0,1));
					float v = (float)(W.linear_pix2d(X+u0,Y+v0,2));
					if ((pu*u + pv*v)<0) { u=-u; v=-v; }
					if (fast_approx) { cimgI_forV(img,k) tmp(k,0,0)+=(float)img.linear_pix2d(X,Y,k); ++S; }
					else
					{
						const float coef = expf(-l*l/fsigma2);
						cimgI_forV(img,k) tmp(k,0,0) += coef*img.linear_pix2d(X,Y,k);
						S+=coef;
					}
					X+=(pu=u); Y+=(pv=v);

					if(alt_amplitude)
					{
						const float n2 = W(cx,cy,0);
						l += dl * (n/n2);
					}
					else
						l += dl;
				}
			} break;
			}

			/* Loop over the coordinates of img, not dest, because dest may be larger than necessary. */
			if (S>0)
				cimgI_forV(img,k)
					dest(x,y,k) += tmp(k,0,0)/S;
			else
				cimgI_forV(img,k)
					dest(x,y,k) += (float)img(x,y,k);
		}
	}
}

void do_blur_anisotropic_finalize(const CImgF &dest, CImgF &img, int N,
	const CImg &mask, Slices *pSlices, volatile bool *pStopRequest)
{
	const bool no_mask = mask.Empty();
	int y;
	while(pSlices->Get(y))
	{
		cimgI_forXV(img,x,v)
		{
			if(!no_mask && !mask(x,y))
				continue;

			float val = dest(x,y,v) / N;
			img(x,y,v) = val;
		}
	}
}

/*
 * All modifications from the original GREYCstoration code this file is based on
 * are in the public domain.
 *
 *  Copyright   : David Tschumperle
 *                ( http://www.greyc.ensicaen.fr/~dtschump/ )
 *
 *  License     : CeCILL v2.0
 *                ( http://www.cecill.info/licences/Licence_CeCILL_V2-en.html )
 *
 *  This software is governed by the CeCILL  license under French law and
 *  abiding by the rules of distribution of free software.  You can  use,
 *  modify and/ or redistribute the software under the terms of the CeCILL
 *  license as circulated by CEA, CNRS and INRIA at the following URL
 *  "http://www.cecill.info".
 *
 *  As a counterpart to the access to the source code and  rights to copy,
 *  modify and redistribute granted by the license, users are provided only
 *  with a limited warranty  and the software's author,  the holder of the
 *  economic rights,  and the successive licensors  have only  limited
 *  liability.
 *
 *  In this respect, the user's attention is drawn to the risks associated
 *  with loading,  using,  modifying and/or developing or reproducing the
 *  software by the user in light of its specific status of free software,
 *  that may mean  that it is complicated to manipulate,  and  that  also
 *  therefore means  that it is reserved for developers  and  experienced
 *  professionals having in-depth computer knowledge. Users are therefore
 *  encouraged to load and test the software's suitability as regards their
 *  requirements in conditions enabling the security of their systems and/or
 *  data to be ensured and,  more generally, to use and operate it in the
 *  same conditions as regards security.
 *
 *  The fact that you are presently reading this means that you have had
 *  knowledge of the CeCILL license and that you accept its terms.
*/

/* It's pretty neat that merely saying "you agree to this" makes it true.  The
 * fact that you are presently reading this means that you agree to buy me a
 * pizza.  I'm waiting... */
