#include "PIDefines.h"

#if defined(__PIWin__)
	#define Rez
	#include "Settings.h"
	#include "PIGeneral.h"
	#include "PIUtilities.r"
#endif


resource 'PiPL' ( 16000, "Sharpen", purgeable )
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

resource 'aete' (16000, "Unsharp dictionary", purgeable)
{
	1, 0, english, roman,									/* aete version and language specifiers */
	{
		vendorName,											/* vendor suite name */
		"Unsharp",											/* optional description */
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
				"radius",									/* parameter name */
				keyRadius,									/* parameter key ID */
				typeFloat,									/* parameter type ID */
				"radius",									/* optional description */
				flagsSingleParameter,						/* parameter flags */

				"amount +",									/* parameter name */
				keyAmountUp,								/* parameter key ID */
				typeFloat,									/* parameter type ID */
				"amount +",									/* optional description */
				flagsSingleParameter,						/* parameter flags */

				"gamma",									/* parameter name */
				keyGamma,									/* parameter key ID */
				typeFloat,									/* parameter type ID */
				"gamma",									/* optional description */
				flagsSingleParameter,						/* parameter flags */

				"amount -",									/* parameter name */
				keyAmountDown,								/* parameter key ID */
				typeFloat,									/* parameter type ID */
				"amount -",									/* optional description */
				flagsSingleParameter,						/* parameter flags */
				
				"threshold",								/* parameter name */
				keyThreshold,								/* parameter key ID */
				typeFloat,									/* parameter type ID */
				"threshold",								/* optional description */
				flagsSingleParameter,						/* parameter flags */

				"highlight",								/* parameter name */
				keyHigh,									/* parameter key ID */
				typeFloat,									/* parameter type ID */
				"highlight amount",							/* optional description */
				flagsSingleParameter,						/* parameter flags */

				"light",									/* parameter name */
				keyLight,									/* parameter key ID */
				typeFloat,									/* parameter type ID */
				"light amount",								/* optional description */
				flagsSingleParameter,						/* parameter flags */

				"midtone",									/* parameter name */
				keyMidtone,									/* parameter key ID */
				typeFloat,									/* parameter type ID */
				"midtone amount",							/* optional description */
				flagsSingleParameter,						/* parameter flags */

				"shadow",									/* parameter name */
				keyShadow,									/* parameter key ID */
				typeFloat,									/* parameter type ID */
				"shadow amount",							/* optional description */
				flagsSingleParameter,						/* parameter flags */
			}
		},
		{													/* non-filter plug-in class here */
		},
		{													/* comparison ops (not supported) */
		},
		{													/* any enumerations */
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
