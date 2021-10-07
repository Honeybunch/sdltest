package com.honeybunch.sdltest;

import org.libsdl.app.SDLActivity;

public class SDLTestActivity extends SDLActivity {
    private static final String TAG = "SDLTest";

    /**
     * This method returns the name of the shared object with the application entry point
     * It can be overridden by derived classes.
     */
    protected String getMainSharedObject() {
        String library = "libsdltest.so";
        return getContext().getApplicationInfo().nativeLibraryDir + "/" + library;
    }

    /**
     * This method is called by SDL before loading the native shared libraries.
     * It can be overridden to provide names of shared libraries to be loaded.
     * The default implementation returns the defaults. It never returns null.
     * An array returned by a new implementation must at least contain "SDL2".
     * Also keep in mind that the order the libraries are loaded may matter.
     * @return names of shared libraries to be loaded (e.g. "SDL2", "main").
     */
    protected String[] getLibraries() {
        return new String[] {
                "hidapi",
                "SDL2",
                "TracyClient",
                "sdltest",
        };
    }
}