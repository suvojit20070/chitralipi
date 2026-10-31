/*
 * Chitralipi for Java and Android - image to text string and back.
 * SPDX-License-Identifier: MIT
 */
package io.github.suvojit20070.chitralipi;

/**
 * Pixels are ARGB ints (same as Android {@code Bitmap.getPixels} and
 * {@code java.awt.image.BufferedImage.getRGB}).
 *
 * <pre>
 * // Android
 * int[] px = new int[w * h];
 * bitmap.getPixels(px, 0, w, 0, 0, w, h);
 * String text = Chitralipi.encode(px, w, h, new Chitralipi.Options().maxChars(4000));
 *
 * Chitralipi.Image img = Chitralipi.decode(text);
 * Bitmap back = Bitmap.createBitmap(img.argb, img.width, img.height, Bitmap.Config.ARGB_8888);
 * </pre>
 *
 * Errors throw {@link IllegalArgumentException} with the reason.
 */
public final class Chitralipi {

    static {
        System.loadLibrary("chitralipi");
    }

    private Chitralipi() {}

    /** Encoder settings. Chain the setters. */
    public static final class Options {
        int quality = 80;
        long maxChars = 0;
        boolean ascii = false;
        boolean allowResize = true;
        int levels = 5;

        /** 1..100, 100 = lossless pixels. Default 80. */
        public Options quality(int q) { quality = q; return this; }

        /** Whole string fits in this many characters (0 = no limit). */
        public Options maxChars(long n) { maxChars = n; return this; }

        /** true = "CHL1a:" Base64URL text (ASCII only, longer). */
        public Options ascii(boolean a) { ascii = a; return this; }

        /** With maxChars, allow shrinking the image to fit. Default true. */
        public Options allowResize(boolean r) { allowResize = r; return this; }

        /** Wavelet levels 0..15. Default 5. */
        public Options levels(int l) { levels = l; return this; }
    }

    /** Decoded image. */
    public static final class Image {
        public final int width;
        public final int height;
        public final int[] argb;

        Image(int width, int height, int[] argb) {
            this.width = width;
            this.height = height;
            this.argb = argb;
        }
    }

    public static String encode(int[] argb, int width, int height) {
        return encode(argb, width, height, new Options());
    }

    public static String encode(int[] argb, int width, int height, Options o) {
        return nativeEncode(argb, width, height, o.quality, o.maxChars, o.ascii, o.allowResize, o.levels);
    }

    public static Image decode(String text) {
        int[] raw = nativeDecode(text);
        int w = raw[0], h = raw[1];
        int[] px = new int[w * h];
        System.arraycopy(raw, 2, px, 0, px.length);
        return new Image(w, h, px);
    }

    /** Any file to text, byte-for-byte exact. */
    public static String pack(byte[] file) { return nativePack(file, false); }

    public static String pack(byte[] file, boolean ascii) { return nativePack(file, ascii); }

    /** Text from {@link #pack} back to the original bytes. */
    public static byte[] unpack(String text) { return nativeUnpack(text); }

    /** Characters as chat apps count them (all Chitralipi characters are single UTF-16 units). */
    public static int charCount(String text) { return text.codePointCount(0, text.length()); }

    public static String version() { return nativeVersion(); }

    private static native String nativeEncode(int[] argb, int width, int height, int quality, long maxChars,
                                              boolean ascii, boolean allowResize, int levels);

    private static native int[] nativeDecode(String text);

    private static native String nativePack(byte[] file, boolean ascii);

    private static native byte[] nativeUnpack(String text);

    private static native String nativeVersion();
}
