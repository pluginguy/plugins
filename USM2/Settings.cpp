#include "Settings.h"
#include "Helpers.h"
#include "PhotoshopHelpers.h"

template<typename T>
class Suite
{
public:
	SPAPI SPErr (*AcquireSuite)( const char *name, long version, const void **suite );

	T* operator->() { return p; }
	Suite(const char *szName, long iVersion)
	{
		m_szName = szName;
		m_iVersion = iVersion;

		SPBasicSuite *pBasicSuite = gFilterRecord->sSPBasic;
		if(pBasicSuite == NULL)
			throw ExceptionSPErr("gFilterRecord->sSPBasic == NULL", errPlugInHostInsufficient);

		SPErr err = pBasicSuite->AcquireSuite(m_szName, m_iVersion, (const void **) &p);
		if(err)
			throw ExceptionSPErr(StringUtil::ssprintf("AcquireSuite(%s, i)", m_szName, m_iVersion), err);
	}

	~Suite()
	{
		SPBasicSuite *pBasicSuite = gFilterRecord->sSPBasic;
		pBasicSuite->ReleaseSuite(m_szName, m_iVersion);
	}

private:
	T *p;
	const char *m_szName;
	long m_iVersion;
};

namespace
{
	const int DisplayModeToScript[] =
	{
		displayModeNormal,
		displayModeInside,
		displayModeSideBySide,
		-1
	};

	Algorithm::Options::DisplayMode ScriptToDisplayMode(int iVal)
	{
		for(int i = 0; DisplayModeToScript[i] != -1; ++i)
			if(DisplayModeToScript[i] == iVal)
				return (Algorithm::Options::DisplayMode) i;
		return (Algorithm::Options::DisplayMode) 0;
	}
}

struct DescriptorRegistryPIActionDescriptor
{
	DescriptorRegistryPIActionDescriptor(Suite<PSActionDescriptorProcs> &pProcs):
		m_Descriptor(NULL), m_pProcs(pProcs)
	{
	}

	~DescriptorRegistryPIActionDescriptor()
	{
		if(m_Descriptor)
			m_pProcs->Free(m_Descriptor);
	}

	PIActionDescriptor operator*() { return m_Descriptor; }

	PIActionDescriptor m_Descriptor;
	Suite<PSActionDescriptorProcs> &m_pProcs;
};

class GetKeys
{
public:
	virtual ~GetKeys() {}
	virtual bool GetNextKey(DescriptorTypeID &type) = 0;
	virtual void Rewind() = 0;
	DescriptorKeyID virtual GetCurKey() const = 0;
	virtual int GetInteger() = 0;
	virtual float GetFloat() = 0;
	virtual float GetPercent() = 0;
	virtual bool GetBoolean() = 0;
	virtual DescriptorEnumID GetEnum() = 0;
};

class GetKeysRegistry: public GetKeys
{
public:
	GetKeysRegistry();
	bool GetNextKey(DescriptorTypeID &type);
	void Rewind();
	DescriptorKeyID GetCurKey() const { return m_CurKey; }
	int GetInteger();
	float GetFloat();
	float GetPercent() { return GetFloat(); }
	bool GetBoolean();
	DescriptorEnumID GetEnum();

private:
	Suite<PSDescriptorRegistryProcs> m_RegistryProcs;
	Suite<PSActionDescriptorProcs> m_DescriptorProcs;
	DescriptorRegistryPIActionDescriptor m_Descriptor;

	uint32 m_iNumKeys;
	DescriptorKeyID m_CurKey;
	uint32 m_iNextKey;
};

GetKeysRegistry::GetKeysRegistry():
	m_RegistryProcs(kPSDescriptorRegistrySuite, kPSDescriptorRegistrySuiteVersion),
	m_DescriptorProcs(kPSActionDescriptorSuite, kPSActionDescriptorSuiteVersion),
	m_Descriptor(m_DescriptorProcs)
{
	SPBasicSuite *m_pBasicSuite = gFilterRecord->sSPBasic;
	if(m_pBasicSuite == NULL)
		throw ExceptionSPErr("gFilterRecord->sSPBasic is NULL", errPlugInHostInsufficient);

	SPErr err = m_RegistryProcs->Get(plugInUniqueID, &m_Descriptor.m_Descriptor);
	if(err)
		throw ExceptionSPErr("m_RegistryProcs->Get", err);
	if(m_Descriptor.m_Descriptor == NULL)
		m_iNumKeys = 0;
	else
		m_DescriptorProcs->GetCount(*m_Descriptor, &m_iNumKeys);
	m_iNextKey = 0;
}

bool GetKeysRegistry::GetNextKey(DescriptorTypeID &type)
{
	if(m_iNextKey == m_iNumKeys)
		return false;

	OSErr err = m_DescriptorProcs->GetKey(*m_Descriptor, m_iNextKey, &m_CurKey);
	if(err)
		throw ExceptionSPErr(StringUtil::ssprintf("GetKeysRegistry::GetKey(%i)", m_iNextKey), err);
	err = m_DescriptorProcs->GetType(*m_Descriptor, m_CurKey, &type);
	if(err)
		throw ExceptionSPErr(StringUtil::ssprintf("GetKeysRegistry::GetType(%08x)", m_CurKey), err);

	++m_iNextKey;
	return true;
}

void GetKeysRegistry::Rewind()
{
	m_iNextKey = 0;
}

int GetKeysRegistry::GetInteger()
{
	int32 iValue;
	OSErr iErr = m_DescriptorProcs->GetInteger(*m_Descriptor, m_CurKey, &iValue);
	if(iErr)
		throw ExceptionSPErr(StringUtil::ssprintf("GetKeysRegistry::GetInteger(%08x)", m_CurKey), iErr);
	return iValue;
}

float GetKeysRegistry::GetFloat()
{
	double fValue;
	OSErr iErr = m_DescriptorProcs->GetFloat(*m_Descriptor, m_CurKey, &fValue);
	if(iErr)
		throw ExceptionSPErr(StringUtil::ssprintf("GetKeysRegistry::GetFloat(%08x)", m_CurKey), iErr);
	return (float) fValue;
}

bool GetKeysRegistry::GetBoolean()
{
	Boolean bValue;
	OSErr iErr = m_DescriptorProcs->GetBoolean(*m_Descriptor, m_CurKey, &bValue);
	if(iErr)
		throw ExceptionSPErr(StringUtil::ssprintf("GetKeysRegistry::GetBoolean(%08x)", m_CurKey), iErr);
	return !!bValue;
}

DescriptorEnumID GetKeysRegistry::GetEnum()
{
	DescriptorEnumID iValue;
	DescriptorEnumTypeID type;
	OSErr iErr = m_DescriptorProcs->GetEnumerated(*m_Descriptor, m_CurKey, &type, &iValue);
	if(iErr)
		throw ExceptionSPErr(StringUtil::ssprintf("GetKeysRegistry::GetEnumerated(%08x)", m_CurKey), iErr);
	return iValue;
}

class GetKeysAction: public GetKeys
{
public:
	GetKeysAction();
	~GetKeysAction();
	bool GetNextKey(DescriptorTypeID &type);
	void Rewind();
	DescriptorKeyID GetCurKey() const { return m_CurKey; }
	int GetInteger();
	float GetFloat();
	float GetPercent() { return GetFloat() / 100.0f; }
	bool GetBoolean();
	DescriptorEnumID GetEnum();

private:
	PIDescriptorParameters *m_pDescParams;
	ReadDescriptorProcs *m_pReadProcs;
	PIReadDescriptor m_Token;
	DescriptorKeyID m_CurKey;
};

GetKeysAction::GetKeysAction()
{
	m_pDescParams = gFilterRecord->descriptorParameters;
	if(m_pDescParams == NULL) return;
	if(m_pDescParams->descriptor == NULL)
	{
		m_pDescParams = NULL;
		return;
	}

	m_pReadProcs = gFilterRecord->descriptorParameters->readDescriptorProcs;
	if(m_pReadProcs == NULL)
		return;
	
	m_Token = NULL;
	Rewind();
}

GetKeysAction::~GetKeysAction()
{
	if(m_pDescParams == NULL)
		return;
	m_pReadProcs->closeReadDescriptorProc(m_Token);
	gFilterRecord->handleProcs->disposeProc(m_pDescParams->descriptor);
	m_pDescParams->descriptor = NULL;
}

void GetKeysAction::Rewind()
{
	if(m_Token)
		m_pReadProcs->closeReadDescriptorProc(m_Token);

	m_Token = m_pReadProcs->openReadDescriptorProc(m_pDescParams->descriptor, NULL);
}

int GetKeysAction::GetInteger()
{
	int32 iValue;
	OSErr iErr = m_pReadProcs->getIntegerProc(m_Token, &iValue);
	if(iErr)
		throw ExceptionSPErr(StringUtil::ssprintf("GetKeysAction::GetInteger(%08x)", m_CurKey), iErr);
	return iValue;
}

float GetKeysAction::GetFloat()
{
	double fValue;
	DescriptorUnitID units;
	OSErr iErr = m_pReadProcs->getUnitFloatProc(m_Token, &units, &fValue);
	if(iErr)
		throw ExceptionSPErr(StringUtil::ssprintf("GetKeysAction::GetFloat(%08x)", m_CurKey), iErr);
	return (float) fValue;
}

bool GetKeysAction::GetBoolean()
{
	boolean bValue;
	OSErr iErr = m_pReadProcs->getBooleanProc(m_Token, &bValue);
	if(iErr)
		throw ExceptionSPErr(StringUtil::ssprintf("GetKeysAction::GetBoolean(%08x)", m_CurKey), iErr);
	return !!bValue;
}

DescriptorEnumID GetKeysAction::GetEnum()
{
	DescriptorEnumID iValue;
	OSErr iErr = m_pReadProcs->getEnumeratedProc(m_Token, &iValue);
	if(iErr)
		throw ExceptionSPErr(StringUtil::ssprintf("GetKeysAction::GetEnum(%08x)", m_CurKey), iErr);
	return iValue;
}

bool GetKeysAction::GetNextKey(DescriptorTypeID &type)
{
	if(m_pDescParams == NULL)
		return false;
	int32 flags = 0;
	return !!m_pReadProcs->getKeyProc(m_Token, &m_CurKey, &type, &flags);
}

class PutKeys
{
public:
	virtual ~PutKeys() { }
	virtual void Finish() = 0;
	virtual void PutInteger(DescriptorKeyID key, int i) = 0;
	virtual void PutFloat(DescriptorKeyID key, float f, DescriptorUnitID units) = 0;
	virtual void PutPercent(DescriptorKeyID key, float f) = 0;
	virtual void PutBoolean(DescriptorKeyID key, bool b) = 0;
	virtual void PutEnum(DescriptorKeyID key, DescriptorEnumID e, DescriptorEnumTypeID type) = 0;
};

class PutKeysAction: public PutKeys
{
public:
	PutKeysAction();
	~PutKeysAction();
	void Finish() { }
	void PutInteger(DescriptorKeyID key, int i);
	void PutFloat(DescriptorKeyID key, float f, DescriptorUnitID units);
	void PutPercent(DescriptorKeyID key, float f);
	void PutBoolean(DescriptorKeyID key, bool b);
	void PutEnum(DescriptorKeyID key, DescriptorEnumID e, DescriptorEnumTypeID type);

private:
	PIDescriptorParameters *m_pDescParams;
	WriteDescriptorProcs *writeProcs;
	PIWriteDescriptor m_Token;
};

PutKeysAction::PutKeysAction()
{
	OSErr err = noErr;

	m_pDescParams = gFilterRecord->descriptorParameters;
	if(m_pDescParams == NULL)
		return;
	
	writeProcs = gFilterRecord->descriptorParameters->writeDescriptorProcs;
	if(writeProcs == NULL)
		return;

	m_Token = writeProcs->openWriteDescriptorProc();
	if(m_Token == NULL)
		return;
}

PutKeysAction::~PutKeysAction()
{
	if(m_pDescParams == NULL)
		return;

	gFilterRecord->handleProcs->disposeProc(m_pDescParams->descriptor);
	writeProcs->closeWriteDescriptorProc(m_Token, &m_pDescParams->descriptor);
	m_pDescParams->recordInfo = plugInDialogOptional;
}

void PutKeysAction::PutInteger(DescriptorKeyID key, int i)
{
	SPErr iErr = writeProcs->putIntegerProc(m_Token, key, i);
	if(iErr)
		throw ExceptionSPErr(StringUtil::ssprintf("PutKeysAction::PutInteger(%08x)", key), iErr);
}

void PutKeysAction::PutFloat(DescriptorKeyID key, float f, DescriptorUnitID units)
{
	double ff = f;
	SPErr iErr = writeProcs->putUnitFloatProc(m_Token, key, units, &ff);
	if(iErr)
		throw ExceptionSPErr(StringUtil::ssprintf("PutKeysAction::PutFloat(%08x)", key), iErr);
}

void PutKeysAction::PutPercent(DescriptorKeyID key, float f)
{
	PutFloat(key, f * 100.0f, unitPercent);
}

void PutKeysAction::PutBoolean(DescriptorKeyID key, bool b)
{
	SPErr iErr = writeProcs->putBooleanProc(m_Token, key, b);
	if(iErr)
		throw ExceptionSPErr(StringUtil::ssprintf("PutKeysAction::PutBoolean(%08x)", key), iErr);
}

void PutKeysAction::PutEnum(DescriptorKeyID key, DescriptorEnumID e, DescriptorEnumTypeID type)
{
	SPErr iErr = writeProcs->putEnumeratedProc(m_Token, key, type, e);
	if(iErr)
		throw ExceptionSPErr(StringUtil::ssprintf("PutKeysAction::PutEnum(%08x)", key), iErr);
}

class PutKeysRegistry: public PutKeys
{
public:
	PutKeysRegistry();
	~PutKeysRegistry();
	void Finish();
	void PutInteger(DescriptorKeyID key, int i);
	void PutFloat(DescriptorKeyID key, float f, DescriptorUnitID units);
	void PutPercent(DescriptorKeyID key, float f);
	void PutBoolean(DescriptorKeyID key, bool b);
	void PutEnum(DescriptorKeyID key, DescriptorEnumID e, DescriptorEnumTypeID type);

private:
	SPBasicSuite *m_pBasicSuite;
	Suite<PSDescriptorRegistryProcs> m_RegistryProcs;
	Suite<PSActionDescriptorProcs> m_DescriptorProcs;
	DescriptorRegistryPIActionDescriptor m_Descriptor;
};

void PutKeysRegistry::PutInteger(DescriptorKeyID key, int i)
{
	SPErr iErr = m_DescriptorProcs->PutInteger(*m_Descriptor, key, i);
	if(iErr)
		throw ExceptionSPErr(StringUtil::ssprintf("PutKeysRegistry::PutInteger(%08x)", key), iErr);
}

void PutKeysRegistry::PutFloat(DescriptorKeyID key, float f, DescriptorUnitID units)
{
	SPErr iErr = m_DescriptorProcs->PutUnitFloat(*m_Descriptor, key, units, f);
	if(iErr)
		throw ExceptionSPErr(StringUtil::ssprintf("PutKeysRegistry::PutFloat(%08x)", key), iErr);
}
void PutKeysRegistry::PutPercent(DescriptorKeyID key, float f)
{
	SPErr iErr = m_DescriptorProcs->PutFloat(*m_Descriptor, key, f);
	if(iErr)
		throw ExceptionSPErr(StringUtil::ssprintf("PutKeysRegistry::PutPercent(%08x)", key), iErr);
}
void PutKeysRegistry::PutBoolean(DescriptorKeyID key, bool b)
{
	SPErr iErr = m_DescriptorProcs->PutBoolean(*m_Descriptor, key, b);
	if(iErr)
		throw ExceptionSPErr(StringUtil::ssprintf("PutKeysRegistry::PutBoolean(%08x)", key), iErr);
}
void PutKeysRegistry::PutEnum(DescriptorKeyID key, DescriptorEnumID e, DescriptorEnumTypeID type)
{
	SPErr iErr = m_DescriptorProcs->PutEnumerated(*m_Descriptor, key, type, e);
	if(iErr)
		throw ExceptionSPErr(StringUtil::ssprintf("PutKeysRegistry::PutEnum(%08x)", key), iErr);
}

PutKeysRegistry::PutKeysRegistry():
	m_RegistryProcs(kPSDescriptorRegistrySuite, kPSDescriptorRegistrySuiteVersion),
	m_DescriptorProcs(kPSActionDescriptorSuite, kPSActionDescriptorSuiteVersion),
	m_Descriptor(m_DescriptorProcs)
{
	m_pBasicSuite = gFilterRecord->sSPBasic;
	if(m_pBasicSuite == NULL)
		throw ExceptionSPErr("gFilterRecord->sSPBasic == NULL", errPlugInHostInsufficient);

	SPErr err = m_DescriptorProcs->Make(&m_Descriptor.m_Descriptor);
	if(err || m_Descriptor.m_Descriptor == NULL)
		throw ExceptionSPErr("m_DescriptorProcs->Make()", err);

}

void PutKeysRegistry::Finish()
{
	SPErr iErr = m_RegistryProcs->Register(plugInUniqueID, *m_Descriptor, true);
	if(iErr)
		throw ExceptionSPErr("m_RegistryProcs->Register", iErr);
}

PutKeysRegistry::~PutKeysRegistry()
{
}

static void ReadFromKeys(GetKeys &keys, Parameters &params, bool bReadOptions)
{
	DescriptorTypeID type;
	while(keys.GetNextKey(type))
	{
		switch(keys.GetCurKey())
		{
		case keyRadius:		params.FilterSettings.fRadius = keys.GetFloat(); break;
		case keyAmountUp:	params.FilterSettings.fAmountUp = keys.GetFloat(); break;
		case keyGamma:		params.FilterSettings.fGamma = keys.GetFloat(); break;
		case keyAmountDown:	params.FilterSettings.fAmountDown = keys.GetFloat(); break;
		case keyThreshold:	params.FilterSettings.fThreshold = keys.GetPercent(); break;
		case keyHigh:		params.FilterSettings.fHigh = keys.GetPercent(); break;
		case keyLight:		params.FilterSettings.fLight = keys.GetPercent(); break;
		case keyMidtone:	params.FilterSettings.fMidtone = keys.GetPercent(); break;
		case keyShadow:		params.FilterSettings.fShadow = keys.GetPercent(); break;
		case keyDisplayMode:
		{
			DescriptorEnumID e = keys.GetEnum();
			params.FilterOptions.m_DisplayMode = ScriptToDisplayMode(e);
			break;
		}

		default:
			break;
		}
	}
}

static void WriteToKeys(PutKeys &keys, const Parameters &params, bool bWriteOptions, bool bOmitDefaults)
{
	const Algorithm::Settings DefaultSettings;
#define TO_SAVE(n) (!bOmitDefaults || params.FilterSettings.n != DefaultSettings.n)
	if(TO_SAVE(fRadius))		keys.PutFloat(keyRadius, params.FilterSettings.fRadius, unitPixels);
	if(TO_SAVE(fAmountUp))		keys.PutFloat(keyAmountUp, params.FilterSettings.fAmountUp, unitPixels);
	if(TO_SAVE(fAmountDown))	keys.PutFloat(keyAmountDown, params.FilterSettings.fAmountDown, unitPixels);
	if(TO_SAVE(fGamma))		keys.PutFloat(keyGamma, params.FilterSettings.fGamma, unitPixels);
	if(TO_SAVE(fThreshold))		keys.PutPercent(keyThreshold, params.FilterSettings.fThreshold);
	if(TO_SAVE(fHigh))		keys.PutPercent(keyHigh, params.FilterSettings.fHigh);
	if(TO_SAVE(fLight))		keys.PutPercent(keyLight, params.FilterSettings.fLight);
	if(TO_SAVE(fMidtone))		keys.PutPercent(keyMidtone, params.FilterSettings.fMidtone);
	if(TO_SAVE(fShadow))		keys.PutPercent(keyShadow, params.FilterSettings.fShadow);

	if(bWriteOptions)
	{
		keys.PutEnum(keyDisplayMode, DisplayModeToScript[params.FilterOptions.m_DisplayMode], typeDisplayMode);
	}

	keys.Finish();
}

void ReadRegistryParameters(Parameters &params)
{
	if(gFilterRecord->sSPBasic == NULL)
		return;

	GetKeysRegistry keys;
	ReadFromKeys(keys, params, true);
}

void WriteRegistryParameters(const Parameters &params)
{
	if(gFilterRecord->sSPBasic == NULL)
		return;

	PutKeysRegistry keys;
	WriteToKeys(keys, params, true, false);
}

void ReadScriptParameters(Parameters &params)
{
	if(gFilterRecord->sSPBasic == NULL)
		return;

	/*
	 * If we have script parameters at all, then we're executing in a script.  Reset
	 * settings, and any existing saved parameters will override the settings.  (This
	 * way, we can just save the settings that have been changed from the default, so
	 * the action details don't show all of the default settings when only one or two
	 * have been changed.)
	 *
	 * If we don't have script parameters, then assume we're not being executed from
	 * a script, any leave the current settings alone.
	 *
	 * This leaves us with a hack: we must always save at least one setting to an
	 * action, even if all of the settings are set to the default, or we won't know
	 * that we're in an action when we run it and we'll run with the registry settings
	 * instead.
	 */
	GetKeysAction keys;
	DescriptorTypeID type;
	if(!keys.GetNextKey(type))
		return;
	keys.Rewind();
	params.FilterSettings = Algorithm::Settings();
	
	ReadFromKeys(keys, params, false);
}

void WriteScriptParameters(const Parameters &params)
{
	if(gFilterRecord->sSPBasic == NULL)
		return;

	PutKeysAction keys;
	WriteToKeys(keys, params, false, true);
}
