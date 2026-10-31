// Run: java -Djava.library.path=<folder with the library> ChitralipiTest
// SPDX-License-Identifier: MIT
import io.github.suvojit20070.chitralipi.Chitralipi;

import java.util.Arrays;

public class ChitralipiTest {
    public static void main(String[] args) {
        int w = 64, h = 40;
        int[] px = new int[w * h];
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                px[y * w + x] = 0xFF000000 | ((x * 4) << 16) | ((y * 6) << 8) | ((x + y) * 2);

        String lossless = Chitralipi.encode(px, w, h, new Chitralipi.Options().quality(100));
        Chitralipi.Image back = Chitralipi.decode(lossless);
        check(back.width == w && back.height == h && Arrays.equals(back.argb, px), "lossless round trip");

        String small = Chitralipi.encode(px, w, h, new Chitralipi.Options().maxChars(300));
        check(Chitralipi.charCount(small) <= 300, "maxChars respected: " + Chitralipi.charCount(small));
        check(small.length() == Chitralipi.charCount(small), "one UTF-16 unit per character");
        Chitralipi.decode(small);

        byte[] file = {(byte) 0xFF, (byte) 0xD8, (byte) 0xFF, 1, 2, 3, 0, 9};
        check(Arrays.equals(Chitralipi.unpack(Chitralipi.pack(file)), file), "pack/unpack");
        check(Arrays.equals(Chitralipi.unpack(Chitralipi.pack(file, true)), file), "pack/unpack ascii");

        try {
            Chitralipi.decode("CHL1:broken");
            check(false, "broken text must throw");
        } catch (IllegalArgumentException expected) {
            System.out.println("broken text -> " + expected.getMessage());
        }
        System.out.println("Java " + Chitralipi.version() + ": all ok. Sample: " + small.substring(0, 20) + "...");
    }

    static void check(boolean ok, String what) {
        if (!ok) throw new AssertionError(what);
    }
}
