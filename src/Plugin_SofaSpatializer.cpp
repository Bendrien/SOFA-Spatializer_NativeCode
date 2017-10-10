#include "AudioPluginUtil.h"
#include "FFTConvolver/FFTConvolver.h"
#include "FFTConvolver/BinauralFFTConvolver.h"
#include "FFTConvolver/TwoStageFFTConvolver.h"

#include <mysofa.h>

#include <math.h>

// A plugin will be encapsulated within a namespace
// This namespace is later used to include the plugin
// in the build with PluginList.h
namespace Plugin_SofaSpatializer {

    /// Communication with unity
    static float current_direction[3];
    static int err;

    extern "C" __declspec(dllexport) void WriteDirection(float *array, int length) {
        for (int i = 0; i < length; ++i) {
            current_direction[i] = array[i];
        }
    }

    extern "C" __declspec(dllexport) int GetErr() {
        return err;
    }

    /// LibMySofa
    const unsigned MAX_SOFA_FILES = 2;

    class SofaContainer {
    public:
        ~SofaContainer() {
            if (is_initialized) {
                for (int i = 0; i < sizeof(this->hrtfs)/sizeof(*this->hrtfs); ++i) {
                    mysofa_free(hrtfs[i]);
                    mysofa_lookup_free(lookups[i]);
                    mysofa_neighborhood_free(neighborhoods[i]);
                }
                //this->is_initialized = false;
            }
        }

        MYSOFA_HRTF* hrtfs[MAX_SOFA_FILES];
        MYSOFA_LOOKUP* lookups[MAX_SOFA_FILES];
        MYSOFA_NEIGHBORHOOD* neighborhoods[MAX_SOFA_FILES];
        int errs[MAX_SOFA_FILES];
        bool is_initialized = false;

        void init(unsigned samplerate) {
            if (!is_initialized) {
                this->is_initialized = true;

                // load sofa files
                // todo: parse specific folder in deterministic order
                for (int i = 0; i < MAX_SOFA_FILES; ++i) {
                    char filename[50];
                    sprintf_s(filename, sizeof(filename), "Assets/Sofa/hrtf%u.sofa", i);
                    hrtfs[i] = mysofa_load(filename, &errs[i]);

                    // generate lookup data
                    mysofa_tocartesian(hrtfs[i]);
                    lookups[i] = mysofa_lookup_init(hrtfs[i]);
                    neighborhoods[i] = mysofa_neighborhood_init(hrtfs[i], lookups[i]);

                    // resample if samplerates doesent match (Warning: long coputationtime!)
                    //if (samplerate != hrtfs[i]->DataSamplingRate.values[i]) { mysofa_resample(hrtfs[i], (float)samplerate); }
                }
            }
        }
    };

    /// Utilities
    static void deinterleave_data(float *in, float *out, int len, int num_ch) {
        for (int ch = 0; ch < num_ch; ++ch) {
            int offset = ch * len;
            for (int i = 0; i < len; ++i) {
                out[offset + i] = in[ch + i*2];
            }
        }
    }

    static void interleave_data(float *in, float *out, int len, int num_ch) {
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

    static SofaContainer sofa;

    // Use an enum for the plugin parameters
    // we want Unity to have access to
    // By default, Unity manipulates exposed
    // parameters with a slider in the editor
    enum Param
    {
        P_SOFA_SELECTOR,
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
        // Editor parameters
        float p[P_NUM];
        // Index of the associated sofafile
        int current_file = 0;
        // Length of the impusle response in samples (assumed to stay the same for each file)
        size_t ir_len = 0;
        // index of the current impulse response
        int current_ir = 0;

        fftconvolver::BinauralFFTConvolver* convolver;
    };

    void InitConvolver(UnityAudioEffectState* state) {
        // Grab the EffectData pointer we added earlier in CreateCallback
        auto *data = state->GetEffectData<EffectData>();

        // Convert editor param to index
        data->current_file = (int)data->p[P_SOFA_SELECTOR] - 1;

        data->ir_len = sofa.hrtfs[data->current_file]->N;

        // check for left right
        if (data->current_ir % 2 == 0) {
            data->convolver->init(state->dspbuffersize,
                                  &sofa.hrtfs[data->current_file]->DataIR.values[data->ir_len * data->current_ir],
                                  &sofa.hrtfs[data->current_file]->DataIR.values[data->ir_len * (data->current_ir + 1)], data->ir_len);
        } else {
            data->convolver->init(state->dspbuffersize,
                                  &sofa.hrtfs[data->current_file]->DataIR.values[data->ir_len * data->current_ir - 1],
                                  &sofa.hrtfs[data->current_file]->DataIR.values[data->ir_len * data->current_ir], data->ir_len);
        }
    }

    // This is a callback we'll have the SDK invoke when initializing parameters
    // Instantiate the parameter details here
    int InternalRegisterEffectDefinition(UnityAudioEffectDefinition& definition)
    {
        definition.paramdefs = new UnityAudioParameterDefinition [P_NUM];
        RegisterParameter(definition,        // EffectDefinition object passed to us
                          "Sofa Selector",   // The parameter label shown in the Unity editor
                          "",                // The units (ex. dB, Hz, cm, s, etc)
                          1.0f,              // Minimum parameter value
                          MAX_SOFA_FILES,    // Maximum parameter value
                          1.0f,              // Default parameter value
                          1.0f,              // Display scale, Unity editor shows actualValue*displayScale
                          1.0f,              // Display exponent, in case you want a slider operating on an exponential scale in the editor
                          P_SOFA_SELECTOR);           // The index of the parameter in question; use the enum value

        // This flag needs to be set if this plugin should be used as the default spatialzer of unity
        //definition.flags |= UnityAudioEffectDefinitionFlags_IsSpatializer;
        return P_NUM;
    }

    // UNITY_AUDIODSP_RESULT is defined as `int`
    // UNITY_AUDIODSP_CALLBACK is defined as nothing
    // So behind the scenes, the function signature is really `int CreateCallback(UnityAudioEffectState* state)`
    // This callback is invoked by Unity when the plugin is loaded
    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK CreateCallback(UnityAudioEffectState* state)
    {
        sofa.init(state->samplerate);

        // Create a new pointer to the struct defined earlier
        auto data = new EffectData;
        // Quickly fill memory location with zeros
        memset(data, 0, sizeof(EffectData));
        // Initialize effectdata with default parameter value(s)
        data->p[P_SOFA_SELECTOR] = 1.0;
        InitParametersFromDefinitions(InternalRegisterEffectDefinition, data->p);

        data->convolver = new fftconvolver::BinauralFFTConvolver();
        // Add our effectdata pointer to the state so we can reach it in other callbacks
        state->effectdata = data;
        InitConvolver(state);

        return UNITY_AUDIODSP_OK;
    }

    // This callback is invoked by Unity when the plugin is unloaded
    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK ReleaseCallback(UnityAudioEffectState* state)
    {
        // Grab the EffectData pointer we added earlier in CreateCallback
        EffectData *data = state->GetEffectData<EffectData>();
        data->convolver->reset();
        delete data->convolver;
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
        if (!sofa.is_initialized) {
            memcpy(outbuffer, inbuffer, length * outchannels * sizeof(float));
            return UNITY_AUDIODSP_OK;
        }

        auto data = state->GetEffectData<EffectData>();

        // prepare data
        // since we have an mono input we just have to deinterleave one channel
        float in_deinterleaved[length];
        deinterleave_data(inbuffer, in_deinterleaved, length, 1);
        float out_deinterleaved[length * inchannels];
        data->convolver->process(&in_deinterleaved[0], &out_deinterleaved[0], &out_deinterleaved[length], length);

        // Get the index to the nearest HRTF direction
        int nearest = mysofa_lookup(sofa.lookups[data->current_file], current_direction);
        // Transform to the first stereo pair index
        if (nearest % 2 != 0) { nearest -= 1; }
        if (data->current_ir != nearest) {
            InitConvolver(state);
            float out_deinterleavedNew[length * inchannels];
            data->convolver->process(&out_deinterleavedNew[0], &out_deinterleavedNew[length], length);

            // Equal power crossfade between both frames
            for (int i = 0; i < length; ++i) {
                float ratio = (float)(i+1) / (float)length;
                float volumeNew = sqrtf(ratio);
                float volumeOld = 1 - volumeNew;

                for (int j = 0; j < inchannels; ++j) {
                    size_t index = (length*j) + i;
                    out_deinterleaved[index] *= volumeOld;
                    out_deinterleaved[index] += out_deinterleavedNew[index] * volumeNew;
                }
            }

            data->current_ir = nearest;
        } //*/

        err = data->current_ir;
        interleave_data(out_deinterleaved, outbuffer, length, outchannels);
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