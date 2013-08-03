#include "PIDefines.h"

#if defined(__PIWin__)
	#define Rez
    #include "PhotoshopKeys.h"
	#include "PIDefines.h"
	#include "PIActions.h"
	#include "PITerminology.h"
	#include "PIGeneral.h"
	#include "PIUtilities.r"
#endif


resource 'PiPL' ( 16000, "GREYCstoration", purgeable )
{
	{
		Kind { Filter },
		Name { plugInName "..." },
		Category { vendorName },
		Version { (latestFilterVersion << 16 ) | latestFilterSubVersion },

		#ifdef __PIWin__
                        #if defined(_WIN64)
                                CodeWin64X86 { "PluginMain" },
                        #else
		                CodeWin32X86 { "PluginMain" },
                        #endif
		#else
			#ifdef BUILDING_FOR_MACH
				CodeMachOPowerPC { 0, 0, "PluginMain" },
			#else
		                CodeCarbonPowerPC { 0, 0, "" },
		        #endif
		#endif

		SupportedModes
		{
			noBitmap, doesSupportGrayScale,
			noIndexedColor, doesSupportRGBColor,
			doesSupportCMYKColor, doesSupportHSLColor,
			doesSupportHSBColor, doesSupportMultichannel,
			doesSupportDuotone, doesSupportLABColor
		},

		HasTerminology
		{
			plugInClassID,
			plugInEventID,
			16000,
			plugInUniqueID
		},
		
		EnableInfo { "in (PSHOP_ImageMode, RGBMode, GrayScaleMode,"
		             "CMYKMode, HSLMode, HSBMode, MultichannelMode,"
					 "DuotoneMode, LabMode, RGB48Mode, Gray16Mode) ||"
					 "PSHOP_ImageDepth == 16"},

		PlugInMaxSize { 2000000, 2000000 },
		
		FilterCaseInfo
		{
			{
				/* Flat data, no selection */
				inWhiteMat, outWhiteMat,
				doNotWriteOutsideSelection,
				filtersLayerMasks, worksWithBlankData,
				copySourceToDestination,
					
				/* Flat data with selection */
				inWhiteMat, outWhiteMat,
				doNotWriteOutsideSelection,
				filtersLayerMasks, worksWithBlankData,
				copySourceToDestination,
				
				/* Floating selection */
				inWhiteMat, outWhiteMat,
				doNotWriteOutsideSelection,
				filtersLayerMasks, worksWithBlankData,
				copySourceToDestination,
					
				/* Editable transparency, no selection */
				inWhiteMat, outWhiteMat,
				doNotWriteOutsideSelection,
				filtersLayerMasks, worksWithBlankData,
				copySourceToDestination,
					
				/* Editable transparency, with selection */
				inWhiteMat, outWhiteMat,
				doNotWriteOutsideSelection,
				filtersLayerMasks, worksWithBlankData,
				copySourceToDestination,
					
				/* Preserved transparency, no selection */
				inWhiteMat, outWhiteMat,
				doNotWriteOutsideSelection,
				filtersLayerMasks, worksWithBlankData,
				copySourceToDestination,
					
				/* Preserved transparency, with selection */
				inWhiteMat, outWhiteMat,
				doNotWriteOutsideSelection,
				filtersLayerMasks, worksWithBlankData,
				copySourceToDestination
			}
		}	
	}
};

resource 'aete' (16000, "GREYCstoration dictionary", purgeable)
{
	1, 0, english, roman,									/* aete version and language specifiers */
	{
		vendorName,											/* vendor suite name */
		"GREYCstoration",									/* optional description */
		plugInSuiteID,										/* suite ID */
		1,													/* suite code, must be 1 */
		1,													/* suite level, must be 1 */
		{													/* structure for filters */
			plugInName,										/* unique filter name */
			plugInAETEComment,								/* optional description */
			plugInClassID,									/* class ID, must be unique or Suite ID */
			plugInEventID,									/* event ID, must be unique to class ID */
			
			NO_REPLY,										/* never a reply */
			IMAGE_DIRECT_PARAMETER,							/* direct parameter, used by Photoshop */
			{												/* parameters here, if any */
				"amplitude",								/* parameter name */
				keyAmplitude,								/* parameter key ID */
				typeFloat,									/* parameter type ID */
				"amplitude",								/* optional description */
				flagsSingleParameter,						/* parameter flags */

				"sharpness",								/* parameter name */
				keySharpness,								/* parameter key ID */
				typeFloat,									/* parameter type ID */
				"contour preservation",						/* optional description */
				flagsSingleParameter,						/* parameter flags */

				"anisotropy",								/* parameter name */
				keyAnisotropy,								/* parameter key ID */
				typeFloat,									/* parameter type ID */
				"anisotropy",								/* optional description */
				flagsSingleParameter,						/* parameter flags */

				"initial gaussian",							/* parameter name */
				keyInitialGaussian,							/* parameter key ID */
				typeFloat,									/* parameter type ID */
				"initial simple blurring",					/* optional description */
				flagsSingleParameter,						/* parameter flags */
				
				"alpha",									/* parameter name */
				keyAlpha,									/* parameter key ID */
				typeFloat,									/* parameter type ID */
				"image pre-blurring",						/* optional description */
				flagsSingleParameter,						/* parameter flags */

				"sigma",									/* parameter name */
				keySigma,									/* parameter key ID */
				typeFloat,									/* parameter type ID */
				"sigma",									/* optional description */
				flagsSingleParameter,						/* parameter flags */

				"gfact",									/* parameter name */
				keyGfact,									/* parameter key ID */
				typeFloat,									/* parameter type ID */
				"gfact",									/* optional description */
				flagsSingleParameter,						/* parameter flags */

				"normalize",								/* parameter name */
				keyNormalize,								/* parameter key ID */
				typeFloat,									/* parameter type ID */
				"Pre-normalization",						/* optional description */
				flagsSingleParameter,						/* parameter flags */

				"dl",										/* parameter name */
				keyDl,										/* parameter key ID */
				typeFloat,									/* parameter type ID */
				"spatial discretization",					/* optional description */
				flagsSingleParameter,						/* parameter flags */

				"da",										/* parameter name */
				keyDa,										/* parameter key ID */
				typeFloat,									/* parameter type ID */
				"angular discretization",					/* optional description */
				flagsSingleParameter,						/* parameter flags */

				"gauss precision",							/* parameter name */
				keyGaussPrec,								/* parameter key ID */
				typeFloat,									/* parameter type ID */
				"gauss precision",							/* optional description */
				flagsSingleParameter,						/* parameter flags */

				"interpolation",							/* parameter name */
				keyInterpolation,							/* parameter key ID */
				typeInterpolation,							/* parameter type ID */
				"interpolation",							/* optional description */
				flagsSingleParameter,						/* parameter flags */

				"fast approximation",						/* parameter name */
				keyFastApprox,								/* parameter key ID */
				typeBoolean,								/* parameter type ID */
				"fast approximation",						/* optional description */
				flagsSingleParameter,						/* parameter flags */

				"alt. amplitude",							/* parameter name */
				keyAltAmplitude,							/* parameter key ID */
				typeBoolean,								/* parameter type ID */
				"alternate amplitude calculate",			/* optional description */
				flagsSingleParameter,						/* parameter flags */

				"iterations",								/* parameter name */
				keyIterations,								/* parameter key ID */
				typeInteger,								/* parameter type ID */
				"iterations",								/* optional description */
				flagsSingleParameter,						/* parameter flags */

				"ignore selection",							/* optional parameter */
				keyIgnoreSelection,							/* key ID */
				typeBoolean,								/* type */
				"filter entire image",						/* optional desc */
				flagsSingleParameter,						/* parameter flags */

				"threads",									/* parameter name */
				keyThreads,									/* parameter key ID */
				typeInteger,								/* parameter type ID */
				"threads",									/* optional description */
				flagsSingleParameter,						/* parameter flags */

				"gpu",										/* parameter name */
				keyGPU,										/* parameter key ID */
				typeBoolean,								/* parameter type ID */
				"enable gpu",								/* optional description */
				flagsSingleParameter,						/* parameter flags */

				"display",									/* parameter name */
				keyDisplayMode,								/* parameter key ID */
				typeDisplayMode,							/* parameter type ID */
				"display",									/* optional description */
				flagsSingleParameter						/* parameter flags */
			}
		},
		{													/* non-filter plug-in class here */
		},
		{													/* comparison ops (not supported) */
		},
		{													/* any enumerations */
			typeInterpolation,
			{
				"nearest",
				interpolationNearest,
				"nearest-neighbor",

				"linear",
				interpolationLinear,
				"linear",

				"runge-kutta",
				interpolationRungeKutta,
				"runge-kutta"
			},
			
			typeDisplayMode,
			{
				"Normal",
				displayModeNormal,
				"Normal",

				"Inside",
				displayModeInside,
				"Inside",

				"Side by side",
				displayModeSideBySide,
				"Side by side"
			}
		}
	}
};
