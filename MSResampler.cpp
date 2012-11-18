#include "MSResampler.h"
#include "CMediaBuffer.h"
#include "win32util.h"
#include "cautil.h"
#include <wmcodecdsp.h>
#include <mmreg.h>
#include <uuids.h>
#include <propsys.h>

_COM_SMARTPTR_TYPEDEF(IWMResamplerProps, __uuidof(IWMResamplerProps));
_COM_SMARTPTR_TYPEDEF(IPropertyStore, __uuidof(IPropertyStore));

WAVEFORMATEXTENSIBLE buildWFXFromASBD(const AudioStreamBasicDescription &asbd)
{
    WAVEFORMATEXTENSIBLE wfx = { 0 };
    WAVEFORMATEX &wf = wfx.Format;
    wf.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wf.nChannels = asbd.mChannelsPerFrame;
    wf.nSamplesPerSec = asbd.mSampleRate;
    wf.wBitsPerSample = asbd.mBytesPerFrame / asbd.mChannelsPerFrame * 8;
    wf.nBlockAlign = asbd.mBytesPerFrame;
    wf.nAvgBytesPerSec = wf.nBlockAlign * wf.nSamplesPerSec;
    wf.cbSize = 22;
    wfx.Samples.wValidBitsPerSample = asbd.mBitsPerChannel;
    wfx.dwChannelMask = 0;
    if (asbd.mFormatFlags & kAudioFormatFlagIsSignedInteger)
	wfx.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    else
	wfx.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    return wfx;
}

void mediaTypeFromASBD(const AudioStreamBasicDescription &asbd,
		       std::shared_ptr<DMO_MEDIA_TYPE> *mediaType)
{
    size_t size = sizeof(DMO_MEDIA_TYPE) + sizeof(WAVEFORMATEXTENSIBLE);
    void *memory = std::calloc(1, size);
    if (!memory) throw std::bad_alloc();
    DMO_MEDIA_TYPE *pmt = static_cast<DMO_MEDIA_TYPE *>(memory);
    std::shared_ptr<DMO_MEDIA_TYPE> result(pmt, std::free);
    BYTE *pb = static_cast<BYTE*>(memory);
    WAVEFORMATEXTENSIBLE *wfxp =
	reinterpret_cast<WAVEFORMATEXTENSIBLE *>(pb + sizeof(DMO_MEDIA_TYPE));
    WAVEFORMATEXTENSIBLE wfx = buildWFXFromASBD(asbd);
    std::memcpy(wfxp, &wfx, sizeof wfx);
    pmt->majortype = MEDIATYPE_Audio;
    pmt->subtype = MEDIASUBTYPE_PCM;
    pmt->formattype = FORMAT_WaveFormatEx;
    pmt->cbFormat = sizeof wfx;
    pmt->pbFormat = reinterpret_cast<BYTE*>(wfxp);
    mediaType->swap(result);
}

std::shared_ptr<IMediaBuffer> createMediaBuffer(size_t size)
{
    IMediaBuffer *bp;
    CMediaBuffer::Create(size, &bp);
    struct Releaser {
	static void call(IUnknown *x) { x->Release(); }
    };
    return std::shared_ptr<IMediaBuffer>(bp, Releaser::call);
}

inline
void throwIfError(HRESULT expr, const char *msg)
{
    if (FAILED(expr))
	win32::throw_error(msg, expr);
}
#define HR(expr) (void)(throwIfError((expr), #expr))


MSResampler::MSResampler(const std::shared_ptr<ISource> &src, int rate,
			   int quality, double bandwidth)
    : FilterBase(src),
      m_state_pull(false),
      m_position(0),
      m_length(~0ULL)
{
    const AudioStreamBasicDescription &iasbd = src->getSampleFormat();
    m_asbd = cautil::buildASBDForPCM(rate, iasbd.mChannelsPerFrame,
				     32, kAudioFormatFlagIsFloat);
    m_length = src->length();
    if (m_length != ~0ULL)
	m_length = m_length * rate / iasbd.mSampleRate;

    HR(m_resampler.CreateInstance(CLSID_CResamplerMediaObject));
    {
	std::shared_ptr<DMO_MEDIA_TYPE> mediaType;
	mediaTypeFromASBD(iasbd, &mediaType);
	HR(m_resampler->SetInputType(0, mediaType.get(), 0));
    }
    {
	std::shared_ptr<DMO_MEDIA_TYPE> mediaType;
	mediaTypeFromASBD(m_asbd, &mediaType);
	HR(m_resampler->SetOutputType(0, mediaType.get(), 0));
    }
    {
	IWMResamplerPropsPtr wmprops = m_resampler;
	HR(wmprops->SetHalfFilterLength(quality));
    }
    {
	IPropertyStorePtr props = m_resampler;
	PROPVARIANT value = { 0 };
	value.vt = VT_R4;
	value.fltVal = bandwidth;
	HR(props->SetValue(MFPKEY_WMRESAMP_LOWPASS_BANDWIDTH, value));
    }
}

size_t MSResampler::readSamples(void *buffer, size_t nsamples)
{
    const AudioStreamBasicDescription &iasbd = source()->getSampleFormat();
    if (!m_state_pull) {
	std::shared_ptr<IMediaBuffer> ibptr
	    = createMediaBuffer(iasbd.mBytesPerFrame * 4096);
	BYTE *bp;
	ibptr->GetBufferAndLength(&bp, 0);
	size_t n = source()->readSamples(bp, 4096);
	if (n > 0) {
	    ibptr->SetLength(n * iasbd.mBytesPerFrame);
	    HR(m_resampler->ProcessInput(0, ibptr.get(), 0, 0, 0));
	} else {
	    m_resampler->Discontinuity(0);
	    m_state_pull = true;
	}
    }
    DMO_OUTPUT_DATA_BUFFER dodb = { 0 };
    std::shared_ptr<IMediaBuffer> obptr
	= createMediaBuffer(m_asbd.mBytesPerFrame * nsamples);
    dodb.pBuffer = obptr.get();
    DWORD status = 0;
    HR(m_resampler->ProcessOutput(0, 1, &dodb, &status));
    m_state_pull = (dodb.dwStatus & DMO_OUTPUT_DATA_BUFFERF_INCOMPLETE);

    BYTE *bp;
    DWORD size;
    obptr->GetBufferAndLength(&bp, &size);
    std::memcpy(buffer, bp, size);
    m_position += size / m_asbd.mBytesPerFrame;
    return size / m_asbd.mBytesPerFrame;
}

