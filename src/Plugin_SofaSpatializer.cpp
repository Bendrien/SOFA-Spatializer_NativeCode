#include "AudioPluginUtil.h"
#include "FFTConvolver/FFTConvolver.h"
#include "FFTConvolver/TwoStageFFTConvolver.h"

#include <utility>

#include <mysofa.h>

//#include <ATK/Special/ConvolutionFilter.h>

// Our plugin will be encapsulated within a namespace
// This namespace is later used to indicate that we
// want to include this plugin in the build with PluginList.h
namespace Plugin_SofaSpatializer {

    /////////////////////////////////////////
    /// Utilities
    ///////////////////////////////////////

    const unsigned MAX_SOFA_FILES = 1;
    const unsigned CONVOLVERS = 2;

    class SofaContainer {
    public:
        MYSOFA_HRTF* hrtfs[MAX_SOFA_FILES];
        bool is_initialized = false;

        void init(unsigned samplerate) {
            if (!is_initialized) {
                int err;

                // load sofa files
                hrtfs[0] = mysofa_load("WIEb_S00_R01.sofa", &err);

                // resample if samplerates doesent match (Warning: long coputationtime!)
                //if (samplerate != hrtfs[0]->DataSamplingRate.values[0]) { mysofa_resample(hrtfs[0], (float)samplerate); }

                this->is_initialized = true;
            }
        }

        void free() {
            if (is_initialized) {
                for (int i = 0; i < sizeof(this->hrtfs)/sizeof(*this->hrtfs); ++i) {
                    mysofa_free(hrtfs[i]);
                }
                this->is_initialized = false;
            }
        }
    };

    static SofaContainer sofaContainer;

    static void deinterleaveData(float* in, float* out, int len, int num_ch) {
        for (int ch = 0; ch < num_ch; ++ch) {
            int offset = ch * len;
            for (int i = 0; i < len; ++i) {
                out[offset + i] = in[ch + i*2];
            }
        }
    }

    static void interleaveData(float* in, float* out, int len, int num_ch) {
        for (int ch = 0; ch < num_ch; ++ch) {
            int offset = ch * len;
            for (int i = 0; i < len; ++i) {
                out[ch + i*2] = in[offset + i];
            }
        }
    }

    /////////////////////////////////////////
    /// plugin logic
    ///////////////////////////////////////

    // Use an enum for the plugin parameters
    // we want Unity to have access to
    // By default, Unity manipulates exposed
    // parameters with a slider in the editor
    enum Param
    {
        P_GAIN,
        P_NUM
        // Since enum values start at 0, the last value
        // gives us the total number of parameters in the enum
        // However, we don't use it as an actual parameter
    };

    // Define a struct that will hold the plugin's state
    // Our noise plugin is very simple, so we're only interested
    // in keeping track of the single parameter we have: gain
    struct EffectData
    {
        float p[P_NUM]; // Parameters
        fftconvolver::FFTConvolver* convolver[CONVOLVERS];
        float last;
    };

    // This is a callback we'll have the SDK invoke when initializing parameters
    // Instantiate the parameter details here
    int InternalRegisterEffectDefinition(UnityAudioEffectDefinition& definition)
    {
        definition.paramdefs = new UnityAudioParameterDefinition [P_NUM];
        RegisterParameter(definition,        // EffectDefinition object passed to us
                          "Gain Multiplier", // The parameter label shown in the Unity editor
                          "",                // The units (ex. dB, Hz, cm, s, etc)
                          0.0f,              // Minimum parameter value
                          10.0f,             // Maximum parameter value
                          1.0f,              // Default parameter value
                          1.0f,              // Display scale, Unity editor shows actualValue*displayScale
                          1.0f,              // Display exponent, in case you want a slider operating on an exponential scale in the editor
                          P_GAIN);           // The index of the parameter in question; use the enum value
        definition.flags |= UnityAudioEffectDefinitionFlags_IsSpatializer;
        return P_NUM;
    }

    // UNITY_AUDIODSP_RESULT is defined as `int`
    // UNITY_AUDIODSP_CALLBACK is defined as nothing
    // So behind the scenes, the function signature is really `int CreateCallback(UnityAudioEffectState* state)`
    // This callback is invoked by Unity when the plugin is loaded
    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK CreateCallback(UnityAudioEffectState* state)
    {
        EffectData* data = new EffectData;    // Create a new pointer to the struct defined earlier
        memset(data, 0, sizeof(EffectData));  // Quickly fill memory location with zeros
        data->p[P_GAIN] = 1.0;                // Initialize effectdata with default parameter value(s)
        InitParametersFromDefinitions(InternalRegisterEffectDefinition, data->p);

        sofaContainer.init(state->samplerate);
        auto const len = sofaContainer.hrtfs[0]->N;
        for (int i = 0; i < CONVOLVERS; ++i) {
            data->convolver[i] = new fftconvolver::FFTConvolver();
            data->convolver[i]->init(state->dspbuffersize, &sofaContainer.hrtfs[0]->DataIR.values[len * i], len);
        }

        // Add our effectdata pointer to the state so we can reach it in other callbacks
        state->effectdata = data;

        return UNITY_AUDIODSP_OK;
    }

    // This callback is invoked by Unity when the plugin is unloaded
    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK ReleaseCallback(UnityAudioEffectState* state)
    {
        //sofaContainer.free();

        // Grab the EffectData pointer we added earlier in CreateCallback
        EffectData *data = state->GetEffectData<EffectData>();
        delete data; // Cleanup
        return UNITY_AUDIODSP_OK;
    }

    /////////////////////////////////////////
    /// Soundprocessing
    ///////////////////////////////////////

    // ProcessCallback gets called as long as the plugin is loaded
    // This includes when the editor is not in play mode!
    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK ProcessCallback(
            UnityAudioEffectState* state, // The state gets passed into all callbacks
            float* inbuffer,              // Plugins can be chained together; inbuffer holds the signal incoming from another plugin, or an AudioSource
            float* outbuffer,             // We fill outbuffer with the signal Unity should send to the speakers
            unsigned int length,          // The number of samples the buffers hold
            int inchannels,               // The number of channels the incoming signal uses
            int outchannels)              // The number of channels the outgoing signal uses
    {
        if (!sofaContainer.is_initialized) {
            memcpy(outbuffer, inbuffer, length * outchannels * sizeof(float));
            return UNITY_AUDIODSP_OK;
        }

        auto data = state->GetEffectData<EffectData>();
        if (data->last != data->p[P_GAIN]) {
            unsigned rnd = rand() % sofaContainer.hrtfs[0]->R;
            auto const len = sofaContainer.hrtfs[0]->N;
            for (int i = 0; rnd < CONVOLVERS; ++rnd) {
                data->convolver[rnd]->init(state->dspbuffersize, &sofaContainer.hrtfs[0]->DataIR.values[len * rnd], len);
            }
            data->last = rnd;
            data->p[P_GAIN] = rnd;
        };

        float in_deinterleaved[length * inchannels];
        float out_deinterleaved[length * inchannels];
        deinterleaveData(inbuffer, in_deinterleaved, length, inchannels);
        // left channel
        (*data->convolver[0]).process(&in_deinterleaved[0], &out_deinterleaved[0], length);
        // right channel
        (*data->convolver[1]).process(&in_deinterleaved[length], &out_deinterleaved[length], length);
        interleaveData(out_deinterleaved, outbuffer, length, outchannels);

        return UNITY_AUDIODSP_OK;
    }

    /////////////////////////////////////////
    /// editor parameter manipulation
    ///////////////////////////////////////

    // Called whenever a parameter is changed from the editor
    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK SetFloatParameterCallback(UnityAudioEffectState* state, int index, float value)
    {
        EffectData *data = state->GetEffectData<EffectData>(); // Grab EffectData
        // index should never point to a parameter that we haven't defined
        if (index >= P_NUM) {
            return UNITY_AUDIODSP_ERR_UNSUPPORTED;
        }
        data->p[index] = value; // Set the parameter to the value the editor gave us

        return UNITY_AUDIODSP_OK;
    }

    // Internally used by the SDK to query parameter values, not sure when this is called...
    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK GetFloatParameterCallback(UnityAudioEffectState* state, int index, float* value, char *valuestr)
    {
        EffectData *data = state->GetEffectData<EffectData>();
        if (index >= P_NUM) {
            return UNITY_AUDIODSP_ERR_UNSUPPORTED;
        }
        if (value != NULL) {
            *value = data->p[index];
        }
        if (valuestr != NULL) {
            valuestr[0] = 0;
        }
        return UNITY_AUDIODSP_OK;
    }

    // Also unsure where this is used, but it's required to be defined, so just return from the function as soon as it's called
    int UNITY_AUDIODSP_CALLBACK GetFloatBufferCallback(UnityAudioEffectState* state, const char* name, float* buffer, int numsamples)
    {
        return UNITY_AUDIODSP_OK;
    }
}