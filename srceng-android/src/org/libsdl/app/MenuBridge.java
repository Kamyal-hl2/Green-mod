package org.libsdl.app;

import android.webkit.JavascriptInterface;

// Green Engine v2 — exposed to the menu WebView as "AndroidBridge"
// (see SDLActivity.onCreate: addJavascriptInterface(new MenuBridge(),
// "AndroidBridge")). Facepunch's control.Addons.js and friends call these
// methods directly, e.g. AndroidBridge.onAddonClick(id).
//
// @JavascriptInterface methods run on the WebView's own thread (which is
// the UI thread here), NOT the engine/game thread. Do not touch engine or
// Lua state directly from here — hop through the native method, which is
// responsible for getting back onto the game thread before touching Lua
// (see the JNI implementation notes in lua_vm.h for LuaVM_RunHooks and
// friends — same rule applies to any future ents.*/hook bridge call).
public class MenuBridge {

    @JavascriptInterface
    public void onAddonClick(String addonId) {
        SDLActivity.nativeOnAddonSelected(addonId);
    }

    // Add further @JavascriptInterface methods here as more of
    // garrysmod/html/js/menu/*.js gets wired up (favorites, search,
    // subscribe/unsubscribe once a non-Steam content source exists, etc).
    // Keep each one a thin call into a single nativeOnXxx() method — do not
    // grow business logic here, it belongs on the C++/Lua side.
}
