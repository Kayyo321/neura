// Same algorithm, input, and output as ../neura/nbody.nra: the same
// operations in the same order, so the float64 results agree bit for bit.
public final class NBody {
    private static final int BODIES = 5;
    private static final double DAYS_PER_YEAR = 365.24;

    private final double[] x;
    private final double[] y;
    private final double[] z;
    private final double[] vx;
    private final double[] vy;
    private final double[] vz;
    private final double[] mass;

    // The sun, Jupiter, Saturn, Uranus, and Neptune.
    private NBody() {
        double pi = 3.141592653589793;
        double solarMass = 4.0 * pi * pi;
        x = new double[] {0.0, 4.84143144246472090, 8.34336671824457987, 12.8943695621391310, 15.3796971148509165};
        y = new double[] {0.0, -1.16032004402742839, 4.12479856412430479, -15.1111514016986312, -25.9193146099879641};
        z = new double[] {0.0, -0.103622044471123109, -0.403523417114321381, -0.223307578892655734,
                          0.179258772950371181};
        vx = new double[] {0.0, 1.66007664274403694e-3 * DAYS_PER_YEAR, -2.76742510726862411e-3 * DAYS_PER_YEAR,
                           2.96460137564761618e-3 * DAYS_PER_YEAR, 2.68067772490389322e-3 * DAYS_PER_YEAR};
        vy = new double[] {0.0, 7.69901118419740425e-3 * DAYS_PER_YEAR, 4.99852801234917238e-3 * DAYS_PER_YEAR,
                           2.37847173959480950e-3 * DAYS_PER_YEAR, 1.62824170038242295e-3 * DAYS_PER_YEAR};
        vz = new double[] {0.0, -6.90460016972063023e-5 * DAYS_PER_YEAR, 2.30417297573763929e-5 * DAYS_PER_YEAR,
                           -2.96589568540237556e-5 * DAYS_PER_YEAR, -9.51592254519715870e-5 * DAYS_PER_YEAR};
        mass = new double[] {solarMass, 9.54791938424326609e-4 * solarMass, 2.85885980666130812e-4 * solarMass,
                             4.36624404335156298e-5 * solarMass, 5.15138902046611451e-5 * solarMass};
        double px = 0.0;
        double py = 0.0;
        double pz = 0.0;
        for (int i = 0; i < BODIES; i = i + 1) {
            px = px + vx[i] * mass[i];
            py = py + vy[i] * mass[i];
            pz = pz + vz[i] * mass[i];
        }
        vx[0] = -px / solarMass;
        vy[0] = -py / solarMass;
        vz[0] = -pz / solarMass;
    }

    private void advance(double dt) {
        for (int i = 0; i < BODIES; i = i + 1) {
            for (int j = i + 1; j < BODIES; j = j + 1) {
                double dx = x[i] - x[j];
                double dy = y[i] - y[j];
                double dz = z[i] - z[j];
                double squared = dx * dx + dy * dy + dz * dz;
                double distance = Math.sqrt(squared);
                double magnitude = dt / (squared * distance);
                vx[i] = vx[i] - dx * mass[j] * magnitude;
                vy[i] = vy[i] - dy * mass[j] * magnitude;
                vz[i] = vz[i] - dz * mass[j] * magnitude;
                vx[j] = vx[j] + dx * mass[i] * magnitude;
                vy[j] = vy[j] + dy * mass[i] * magnitude;
                vz[j] = vz[j] + dz * mass[i] * magnitude;
            }
        }
        for (int i = 0; i < BODIES; i = i + 1) {
            x[i] = x[i] + dt * vx[i];
            y[i] = y[i] + dt * vy[i];
            z[i] = z[i] + dt * vz[i];
        }
    }

    private double energy() {
        double e = 0.0;
        for (int i = 0; i < BODIES; i = i + 1) {
            e = e + 0.5 * mass[i] * (vx[i] * vx[i] + vy[i] * vy[i] + vz[i] * vz[i]);
            for (int j = i + 1; j < BODIES; j = j + 1) {
                double dx = x[i] - x[j];
                double dy = y[i] - y[j];
                double dz = z[i] - z[j];
                double distance = Math.sqrt(dx * dx + dy * dy + dz * dz);
                e = e - mass[i] * mass[j] / distance;
            }
        }
        return e;
    }

    public static void main(String[] args) {
        int n = Integer.parseInt(args[0]);
        NBody planets = new NBody();
        double before = planets.energy();
        for (int i = 0; i < n; i = i + 1) {
            planets.advance(0.01);
        }
        double after = planets.energy();
        System.out.println("nbody " + Long.toHexString(Double.doubleToRawLongBits(before)) + " "
                           + Long.toHexString(Double.doubleToRawLongBits(after)));
    }
}
