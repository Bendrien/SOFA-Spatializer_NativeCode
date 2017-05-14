#if UNITY_WIN // Other flags such as UNITY_WIN, UNITY_LINUX, UNITY_OSX, UNITY_PS3 exist
DECLARE_EFFECT("Gain", Plugin_Gain)
// The left argument is what the editor will display as the name for this plugin
// The right argument must match the namespace we use to encapsulate the plugin logic
#endif