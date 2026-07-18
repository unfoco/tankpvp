package dev.embrik;

import org.libsdl.app.SDLActivity;

public class EmbrikActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }
}
