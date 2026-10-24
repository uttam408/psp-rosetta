import java.awt.Color;
import java.awt.Graphics2D;
import java.awt.RenderingHints;
import java.awt.image.BufferedImage;
import java.util.Random;

/* Random integer-vertex polygons via Graphics2D.fillPolygon (AA off): prints
 * "n x0 y0 ... npixels hash" per polygon. */
public class PolyTest {
    public static void main(String[] a) {
        System.setProperty("java.awt.headless", "true");
        int count = Integer.parseInt(a[0]);
        BufferedImage img = new BufferedImage(800, 450, BufferedImage.TYPE_INT_RGB);
        Graphics2D g = img.createGraphics();
        g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_OFF);
        Random r = new Random(7);
        int[] px = new int[800 * 450];
        for (int i = 0; i < count; i++) {
            int n = 3 + r.nextInt(6);
            int span = (i % 2 == 0) ? 40 : 300;
            int ox = r.nextInt(800 - span), oy = r.nextInt(450 - span);
            int[] xs = new int[n], ys = new int[n];
            for (int k = 0; k < n; k++) { xs[k] = ox + r.nextInt(span); ys[k] = oy + r.nextInt(span); }
            g.setColor(Color.BLACK);
            g.fillRect(0, 0, 800, 450);
            g.setColor(Color.WHITE);
            g.fillPolygon(xs, ys, n);
            img.getRGB(0, 0, 800, 450, px, 0, 800);
            long h = 0; int cnt = 0;
            for (int y = 0; y < 450; y++)
                for (int x = 0; x < 800; x++)
                    if ((px[y * 800 + x] & 0xFFFFFF) != 0) { cnt++; h += (long) (x + 1) * 7919L + (long) (y + 1) * 104729L * 31L; }
            StringBuilder sb = new StringBuilder();
            sb.append(n);
            for (int k = 0; k < n; k++) sb.append(' ').append(xs[k]).append(' ').append(ys[k]);
            sb.append(' ').append(cnt).append(' ').append(h);
            System.out.println(sb);
            if (a.length > 1 && Integer.parseInt(a[1]) == i) {
                for (int y = 0; y < 450; y++) {
                    int lo = -1, hi = -1;
                    for (int x = 0; x < 800; x++) if ((px[y * 800 + x] & 0xFFFFFF) != 0) { if (lo < 0) lo = x; hi = x; }
                    if (lo >= 0) System.err.println("R " + y + " " + lo + " " + hi);
                }
            }
        }
        System.exit(0);
    }
}
