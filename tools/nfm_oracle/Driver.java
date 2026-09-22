import java.awt.RenderingHints;
import java.awt.image.BufferedImage;
import java.awt.Graphics2D;
import java.io.*;
import java.nio.file.*;
import java.util.*;
import java.util.zip.*;
import javax.imageio.ImageIO;

/* Oracle: renders a stage with the ORIGINAL Medium/ContO/Plane at 800x450 (no scaling),
 * following GameSparker's directive handling and per-frame draw/sort order.
 *   Driver <stages/N.txt> <models.zip> <out.png> camx camz yaw pitch height [frames] */
public class Driver {
    static String[] cars = { "2000tornados", "formula7", "canyenaro", "lescrab", "nimi", "maxrevenge", "leadoxide", "koolkat", "drifter", "policecops", "mustang", "king", "audir8", "masheen", "radicalone", "drmonster" };
    static String[] pieces = { "road", "froad", "twister2", "twister1", "turn", "offroad", "bumproad", "offturn", "nroad", "nturn", "roblend", "noblend", "rnblend", "roadend", "offroadend", "hpground", "ramp30", "cramp35", "dramp15", "dhilo15", "slide10", "takeoff", "sramp22", "offbump", "offramp", "sofframp", "halfpipe", "spikes", "rail", "thewall", "checkpoint", "fixpoint", "offcheckpoint", "sideoff", "bsideoff", "uprise", "riseroad", "sroad", "soffroad", "tside", "launchpad", "thenet", "speedramp", "offhill", "slider", "uphill", "roll1", "roll2", "roll3", "roll4", "roll5", "roll6", "opile1", "opile2", "aircheckpoint", "tree1", "tree2", "tree3", "tree4", "tree5", "tree6", "tree7", "tree8", "cac1", "cac2", "cac3", "8sroad", "8soffroad" };

    static int[] args(String key, String line) {
        int a = line.indexOf('('), b = line.indexOf(')');
        String[] p = line.substring(a + 1, b).split(",");
        int[] v = new int[p.length];
        for (int i = 0; i < p.length; i++) v[i] = Integer.parseInt(p[i].trim());
        return v;
    }

    public static void main(String[] a) throws Exception {
        System.setProperty("java.awt.headless", "true");
        Medium m = new Medium();
        Trackers t = new Trackers();
        ContO[] models = new ContO[130];
        try (ZipInputStream z = new ZipInputStream(new FileInputStream(a[1]))) {
            for (ZipEntry e = z.getNextEntry(); e != null; e = z.getNextEntry()) {
                int n2 = 0;
                for (int i = 0; i < 16; i++) if (e.getName().startsWith(cars[i])) n2 = i;
                for (int j = 0; j < 68; j++) if (e.getName().startsWith(pieces[j])) n2 = j + 56;
                byte[] buf = z.readAllBytes();
                models[n2] = new ContO(buf, m, t);
            }
        }
        ContO[] objs = new ContO[2000];
        int nob = 0;
        for (String line : Files.readAllLines(Paths.get(a[0]))) {
            line = line.trim();
            if (line.startsWith("snap")) { int[] v = args("snap", line); m.setsnap(v[0], v[1], v[2]); }
            if (line.startsWith("sky")) { int[] v = args("sky", line); m.setsky(v[0], v[1], v[2]); }
            if (line.startsWith("ground")) { int[] v = args("ground", line); m.setgrnd(v[0], v[1], v[2]); }
            if (line.startsWith("polys")) { int[] v = args("polys", line); m.setpolys(v[0], v[1], v[2]); }
            if (line.startsWith("fog")) { int[] v = args("fog", line); m.setfade(v[0], v[1], v[2]); }
            if (line.startsWith("texture")) { int[] v = args("texture", line); m.setexture(v[0], v[1], v[2], v[3]); }
            if (line.startsWith("density")) {
                m.fogd = (args("density", line)[0] + 1) * 2 - 1;
                if (m.fogd < 1) m.fogd = 1;
                if (m.fogd > 30) m.fogd = 30;
            }
            if (line.startsWith("fadefrom")) m.fadfrom(args("fadefrom", line)[0]);
            if (line.startsWith("lightson")) m.lightson = true;
            if (line.startsWith("set")) {
                int[] v = args("set", line);
                int id = v[0] + 46;
                objs[nob++] = new ContO(models[id], v[1], m.ground - models[id].grat, v[2], v[3]);
            }
            if (line.startsWith("chk")) {
                int[] v = args("chk", line);
                int id = v[0] + 46;
                int y = m.ground - models[id].grat;
                if (id == 110) y = v[4];
                objs[nob++] = new ContO(models[id], v[1], y, v[2], v[3]);
            }
            if (line.startsWith("fix")) {
                int[] v = args("fix", line);
                int id = v[0] + 46;
                objs[nob] = new ContO(models[id], v[1], v[3], v[2], v[4]);
                objs[nob++].elec = true;
            }
            if (line.startsWith("pile")) {
                int[] v = args("pile", line);
                objs[nob++] = new ContO(v[0], v[1], v[2], m, t, v[3], v[4], m.ground);
            }
            for (String w : new String[] { "maxr", "maxl", "maxt", "maxb" }) {
                if (!line.startsWith(w)) continue;
                int[] v = args(w, line);
                for (int k = 0; k < v[0]; k++) {
                    int x = (w.equals("maxr") || w.equals("maxl")) ? v[1] : k * 4800 + v[2];
                    int zz = (w.equals("maxr") || w.equals("maxl")) ? k * 4800 + v[2] : v[1];
                    int rot = w.equals("maxr") ? 0 : w.equals("maxl") ? 180 : w.equals("maxt") ? 90 : -90;
                    objs[nob++] = new ContO(models[85], x, m.ground - models[85].grat, zz, rot);
                }
            }
        }
        int camx = Integer.parseInt(a[3]), camz = Integer.parseInt(a[4]), yaw = Integer.parseInt(a[5]);
        int pitch = Integer.parseInt(a[6]), height = Integer.parseInt(a[7]);
        int frames = a.length > 8 ? Integer.parseInt(a[8]) : 3;
        m.x = camx - m.cx; m.z = camz; m.y = -height; m.xz = yaw; m.zy = pitch;

        BufferedImage img = new BufferedImage(800, 450, BufferedImage.TYPE_INT_RGB);
        Graphics2D rd = img.createGraphics();
        rd.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_OFF);
        for (int f = 0; f < frames; f++) {
            m.d(rd);
            int[] list = new int[nob];
            int nl = 0;
            for (int i = 0; i < nob; i++) {
                if (objs[i].dist != 0) list[nl++] = i;
                else objs[i].d(rd);
            }
            int[] rank = new int[nl];
            for (int i = 0; i < nl; i++) {
                for (int j = i + 1; j < nl; j++) {
                    if (objs[list[i]].dist != objs[list[j]].dist) {
                        if (objs[list[i]].dist < objs[list[j]].dist) rank[i]++; else rank[j]++;
                    } else rank[i]++;
                }
            }
            for (int r = 0; r < nl; r++)
                for (int i = 0; i < nl; i++)
                    if (rank[i] == r) objs[list[i]].d(rd);
        }
        ImageIO.write(img, "png", new File(a[2]));
        System.out.println("oracle: " + nob + " objects -> " + a[2]);
        System.exit(0);
    }
}
