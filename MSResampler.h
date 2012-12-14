#ifndef MFRESAMPLER_H
#define MFRESAMPLER_H

#include "iointer.h"
#include "win32util.h"
#include <comdef.h>
#include <mediaobj.h>

_COM_SMARTPTR_TYPEDEF(IMediaObject, __uuidof(IMediaObject));

struct IDMODSPEngine {
    virtual ~IDMODSPEngine() {}
    virtual const AudioStreamBasicDescription &getSampleFormat() const = 0;
    virtual IMediaObjectPtr &mediaObject() = 0;
};

class DMODSPProcessor: public FilterBase {
    bool m_state_pull;
    int64_t m_position;
    uint64_t m_length;
    std::shared_ptr<IDMODSPEngine> m_engine;
public:
    DMODSPProcessor(const std::shared_ptr<ISource> &src, 
                    const std::shared_ptr<IDMODSPEngine> &engine);
    uint64_t length() const { return m_length; }
    const AudioStreamBasicDescription &getSampleFormat() const
    {
        return m_engine->getSampleFormat();
    }
    int64_t getPosition() { return m_position; }
    size_t readSamples(void *buffer, size_t nsamples);
};

class MSResampler: public IDMODSPEngine {
    IMediaObjectPtr m_mediaObject;
    AudioStreamBasicDescription m_asbd;
public:
    MSResampler(const std::shared_ptr<ISource> &src, int rate,
                int quality=60, double bandwidth=0.95);
    const AudioStreamBasicDescription &getSampleFormat() const
    {
        return m_asbd;
    }
    IMediaObjectPtr &mediaObject() { return m_mediaObject; };
};

#endif
