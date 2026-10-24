import java.awt.Color;
import java.awt.Graphics2D;
import java.awt.RenderingHints;
import java.awt.image.BufferedImage;
import java.util.Random;

/* Draws N random segments with Graphics2D.drawLine (AA off) and prints, per segment,
 * "x0 y0 x1 y1 npixels hash" where hash is a position-only checksum of the pixel set. */
public class LineTest {
    public static void main(String[] a) {
        System.setProperty("java.awt.headless", "true");
        int n = Integer.parseInt(a[0]);
        int R = a.length > 1 ? Integer.parseInt(a[1]) : 1;
        BufferedImage img = new BufferedImage(800, 450, BufferedImage.TYPE_INT_RGB);
        Graphics2D g = img.createGraphics();
        g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_OFF);
        Random r = new Random(R);
        int[] px = new int[800 * 450];
        for (int i = 0; i < n; i++) {
            int x0 = r.nextInt(800), y0 = r.nextInt(450), x1 = r.nextInt(800), y1 = r.nextInt(450);
            if (i % 3 == 0) { x1 = x0 + r.nextInt(41) - 20; y1 = y0 + r.nextInt(41) - 20; if (x1 < 0) x1 = 0; if (y1 < 0) y1 = 0; if (x1 > 799) x1 = 799; if (y1 > 449) y1 = 449; }
            g.setColor(Color.BLACK);
            g.fillRect(0, 0, 800, 450);
            g.setColor(Color.WHITE);
            g.drawLine(x0, y0, x1, y1);
            img.getRGB(0, 0, 800, 450, px, 0, 800);
            long h = 0; int cnt = 0;
            for (int y = 0; y < 450; y++)
                for (int x = 0; x < 800; x++)
                    if ((px[y * 800 + x] & 0xFFFFFF) != 0) { cnt++; h += (long) (x + 1) * 7919L + (long) (y + 1) * 104729L * 31L; }
            System.out.println(x0 + " " + y0 + " " + x1 + " " + y1 + " " + cnt + " " + h);
        }
        System.exit(0);
    }
}
