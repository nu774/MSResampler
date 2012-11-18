#ifndef MFRESAMPLER_H
#define MFRESAMPLER_H

#include "iointer.h"
#include "win32util.h"
#include <comdef.h>
#include <mediaobj.h>

_COM_SMARTPTR_TYPEDEF(IMediaObject, __uuidof(IMediaObject));

class MSResampler: public FilterBase {
    bool m_state_pull;
    int64_t m_position;
    uint64_t m_length;
    IMediaObjectPtr m_resampler;
    AudioStreamBasicDescription m_asbd;
public:
    MSResampler(const std::shared_ptr<ISource> &src, int rate,
		 int quality=60, double bandwidth=0.95);
    uint64_t length() const { return m_length; }
    const AudioStreamBasicDescription &getSampleFormat() const
    {
	return m_asbd;
    }
    int64_t getPosition() { return m_position; }
    size_t readSamples(void *buffer, size_t nsamples);
};

#endif
