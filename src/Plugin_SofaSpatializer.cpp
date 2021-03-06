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

    // There cant be more sofa files loaded then this number
    static const unsigned MAX_SOFA_FILES = 10;
    static const int DIR_DIM = 3;

    static int err;

    /// LibMySofa
    class SofaContainer {
    public:
        ~SofaContainer() {
            if (is_initialized) {
                for (int i = 0; i < sizeof(this->hrtfs)/sizeof(*this->hrtfs); ++i) {
                    mysofa_free(hrtfs[i]);
                    mysofa_lookup_free(lookups[i]);
                    mysofa_neighborhood_free(neighborhoods[i]);
                }
                this->is_initialized = false;
            }
        }

        MYSOFA_HRTF* hrtfs[MAX_SOFA_FILES];
        MYSOFA_LOOKUP* lookups[MAX_SOFA_FILES];
        MYSOFA_NEIGHBORHOOD* neighborhoods[MAX_SOFA_FILES];
        int errs[MAX_SOFA_FILES];
        float dirs[DIR_DIM * MAX_SOFA_FILES];
        bool is_initialized = false;


        void init(unsigned samplerate) {
            if (!is_initialized) {
                this->is_initialized = true;

                // load sofa files
                for (int i = 0; i < MAX_SOFA_FILES; ++i) {
                    char filename[50];
                    sprintf_s(filename, sizeof(filename), "Assets/Sofa/hrtf%u.sofa", i);
                    hrtfs[i] = mysofa_load(filename, &errs[i]);

                    if (errs[i] != MYSOFA_OK) {
                        continue;
                    }

                    // Convert to cartesian, initialize the look up
                    mysofa_tocartesian(hrtfs[i]);
                    lookups[i] = mysofa_lookup_init(hrtfs[i]);
                    neighborhoods[i] = mysofa_neighborhood_init(hrtfs[i], lookups[i]);

                    // resample if samplerates doesent match (Warning: long coputationtime!)
                    //if (samplerate != hrtfs[i]->DataSamplingRate.values[i]) { mysofa_resample(hrtfs[i], (float)samplerate); }

                    /// TODO: perfomance can be improved by precomputing hrtfs into the frequency domain
                    /// and let the convolver be initializable with them
                }
            }
        }
    };

    static SofaContainer sofa;

    /// Communication with unity
    extern "C" __declspec(dllexport) void write_direction(float *array, int index) {

        if (index < 0 || index >= MAX_SOFA_FILES) {
            return;
        }

        const int offset = index * DIR_DIM;
        for (int i = 0; i < DIR_DIM; ++i) {
            sofa.dirs[offset + i] = array[i];
        }
    }

    extern "C" __declspec(dllexport) int get_err() {
        return err;
    }

    extern "C" __declspec(dllexport) int get_max_sofa_files() {
        return MAX_SOFA_FILES;
    }

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
        int current_hrtf = 0;
        // Length of the impusle response in samples (assumed to stay the same for each file)
        size_t ir_len = 0;
        // index of the current impulse response
        int current_ir = 0;

        bool is_initialized = false;

        fftconvolver::BinauralFFTConvolver* convolver;
    };

    // This is a callback we'll have the SDK invoke when initializing parameters
    // Instantiate the parameter details here
    int InternalRegisterEffectDefinition(UnityAudioEffectDefinition& definition)
    {
        definition.paramdefs = new UnityAudioParameterDefinition [P_NUM];
        RegisterParameter(definition,        // EffectDefinition object passed to us
                          "Sofa Selector",   // The parameter label shown in the Unity editor
                          "",                // The units (ex. dB, Hz, cm, s, etc)
                          0.0f,              // Minimum parameter value
                          MAX_SOFA_FILES-1,  // Maximum parameter value
                          0.0f,              // Default parameter value
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
        data->convolver = new fftconvolver::BinauralFFTConvolver();
        // Add the effectdata pointer to the state so it can be reached in other callbacks
        state->effectdata = data;
        InitParametersFromDefinitions(InternalRegisterEffectDefinition, data->p);

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

    void init_convolver(UnityAudioEffectState *state) {
        // Grab the EffectData pointer we added earlier in CreateCallback
        auto *data = state->GetEffectData<EffectData>();

        if (data->is_initialized) {
            return;
        }

        // Convert editor param into an index
        auto new_hrtf = (int)data->p[P_SOFA_SELECTOR];

        if (new_hrtf < 0 || new_hrtf >= MAX_SOFA_FILES ||
                sofa.errs[new_hrtf] != MYSOFA_OK) {
            return;
        }

        data->current_hrtf = new_hrtf;

        // Get the index of the nearest HRTF in relation to the direction
        data->current_ir = mysofa_lookup(sofa.lookups[data->current_hrtf],
                                         &sofa.dirs[data->current_hrtf * DIR_DIM]);
        // Transform to the first stereo pair index
        if (data->current_ir % 2 != 0) { data->current_ir -= 1; }

        data->ir_len = sofa.hrtfs[data->current_hrtf]->N;

        data->convolver->init(state->dspbuffersize,
                              &sofa.hrtfs[data->current_hrtf]->DataIR.values[data->current_ir * data->ir_len],
                              &sofa.hrtfs[data->current_hrtf]->DataIR.values[(data->current_ir + 1) * data->ir_len],
                              data->ir_len);
        data->is_initialized = true;
    }

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

        init_convolver(state);
        auto data = state->GetEffectData<EffectData>();

        if (!data->is_initialized) {
            memcpy(outbuffer, inbuffer, length * outchannels * sizeof(float));
            return UNITY_AUDIODSP_OK;
        }

        // Prepare data
        // since we have an mono input we just have to deinterleave one channel
        float in_deinterleaved[length];
        deinterleave_data(inbuffer, in_deinterleaved, length, 1);
        float out_deinterleaved[length * inchannels];
        data->convolver->process(&in_deinterleaved[0], &out_deinterleaved[0], &out_deinterleaved[length], length);

        // Get the index of the nearest HRTF in relation to the direction
        int nearest_ir = mysofa_lookup(sofa.lookups[data->current_hrtf],
                                       &sofa.dirs[data->current_hrtf * DIR_DIM]);
        // Transform to the first stereo pair index
        if (nearest_ir % 2 != 0) { nearest_ir -= 1; }
        if (data->current_ir != nearest_ir) {
            // Init new impulse response
            data->convolver->init(state->dspbuffersize,
                                  &sofa.hrtfs[data->current_hrtf]->DataIR.values[data->current_ir * data->ir_len],
                                  &sofa.hrtfs[data->current_hrtf]->DataIR.values[(data->current_ir + 1) * data->ir_len],
                                  data->ir_len);
            float out_deinterleaved_new[length * inchannels];
            data->convolver->process(&out_deinterleaved_new[0], &out_deinterleaved_new[length], length);

            // Equal power crossfade between the old and new frame mixed into the out buffer
            for (int i = 0; i < length; ++i) {
                float ratio = (float)(i+1) / (float)length;
                float volume_new = sqrtf(ratio);
                float volume_old = 1 - volume_new;

                for (int j = 0; j < inchannels; ++j) {
                    size_t index = (length*j) + i;
                    out_deinterleaved[index] *= volume_old;
                    out_deinterleaved[index] += out_deinterleaved_new[index] * volume_new;
                }
            }

            data->current_ir = nearest_ir;
        } //*/

        //err = sofa.errs[data->current_hrtf];
        err = data->current_ir;
        //err = data->current_hrtf;
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
        if (index == P_SOFA_SELECTOR && (int)value != data->current_hrtf) {
            data->is_initialized = false;
        }
        data->p[index] = value;

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