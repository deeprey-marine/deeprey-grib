/***************************************************************************
 * Force include header for GLEW
 * 
 * This header is automatically included before any other headers via 
 * compiler flags to ensure GLEW is loaded before wxWidgets GL headers
 ***************************************************************************/

#ifndef __GLEW_FORCE_INCLUDE_H__
#define __GLEW_FORCE_INCLUDE_H__

// Include GLEW only on platforms that need it and before any other GL headers
#if defined(__WXQT__) || defined(__WXGTK__)
    #ifndef __glew_h__
        #include <GL/glew.h>
    #endif
#endif

// Define APIENTRY for GLU callbacks if not already defined
#ifndef APIENTRY
    #ifdef _WIN32
        #define APIENTRY __stdcall
    #else
        #define APIENTRY
    #endif
#endif

#endif // __GLEW_FORCE_INCLUDE_H__
