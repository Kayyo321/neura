internal static class NBody
{
    private const int Bodies = 5;
    private const double DaysPerYear = 365.24;

    private sealed class SystemState
    {
        private readonly double[] x;
        private readonly double[] y;
        private readonly double[] z;
        private readonly double[] vx;
        private readonly double[] vy;
        private readonly double[] vz;
        private readonly double[] mass;

        public SystemState()
        {
            double pi = 3.141592653589793;
            double solarMass = 4.0 * pi * pi;
            x = [0.0, 4.84143144246472090, 8.34336671824457987, 12.8943695621391310, 15.3796971148509165];
            y = [0.0, -1.16032004402742839, 4.12479856412430479, -15.1111514016986312, -25.9193146099879641];
            z = [0.0, -0.103622044471123109, -0.403523417114321381, -0.223307578892655734, 0.179258772950371181];
            vx = [0.0, 1.66007664274403694e-3 * DaysPerYear, -2.76742510726862411e-3 * DaysPerYear,
                  2.96460137564761618e-3 * DaysPerYear, 2.68067772490389322e-3 * DaysPerYear];
            vy = [0.0, 7.69901118419740425e-3 * DaysPerYear, 4.99852801234917238e-3 * DaysPerYear,
                  2.37847173959480950e-3 * DaysPerYear, 1.62824170038242295e-3 * DaysPerYear];
            vz = [0.0, -6.90460016972063023e-5 * DaysPerYear, 2.30417297573763929e-5 * DaysPerYear,
                  -2.96589568540237556e-5 * DaysPerYear, -9.51592254519715870e-5 * DaysPerYear];
            mass = [solarMass, 9.54791938424326609e-4 * solarMass, 2.85885980666130812e-4 * solarMass,
                    4.36624404335156298e-5 * solarMass, 5.15138902046611451e-5 * solarMass];
            double px = 0.0;
            double py = 0.0;
            double pz = 0.0;
            for (int i = 0; i < Bodies; i++)
            {
                px += vx[i] * mass[i];
                py += vy[i] * mass[i];
                pz += vz[i] * mass[i];
            }
            vx[0] = -px / solarMass;
            vy[0] = -py / solarMass;
            vz[0] = -pz / solarMass;
        }

        public void Advance(double dt)
        {
            for (int i = 0; i < Bodies; i++)
                for (int j = i + 1; j < Bodies; j++)
                {
                    double dx = x[i] - x[j];
                    double dy = y[i] - y[j];
                    double dz = z[i] - z[j];
                    double squared = dx * dx + dy * dy + dz * dz;
                    double distance = Math.Sqrt(squared);
                    double magnitude = dt / (squared * distance);
                    vx[i] -= dx * mass[j] * magnitude;
                    vy[i] -= dy * mass[j] * magnitude;
                    vz[i] -= dz * mass[j] * magnitude;
                    vx[j] += dx * mass[i] * magnitude;
                    vy[j] += dy * mass[i] * magnitude;
                    vz[j] += dz * mass[i] * magnitude;
                }

            for (int i = 0; i < Bodies; i++)
            {
                x[i] += dt * vx[i];
                y[i] += dt * vy[i];
                z[i] += dt * vz[i];
            }
        }

        public double Energy()
        {
            double energy = 0.0;
            for (int i = 0; i < Bodies; i++)
            {
                energy += 0.5 * mass[i] * (vx[i] * vx[i] + vy[i] * vy[i] + vz[i] * vz[i]);
                for (int j = i + 1; j < Bodies; j++)
                {
                    double dx = x[i] - x[j];
                    double dy = y[i] - y[j];
                    double dz = z[i] - z[j];
                    double distance = Math.Sqrt(dx * dx + dy * dy + dz * dz);
                    energy -= mass[i] * mass[j] / distance;
                }
            }
            return energy;
        }
    }

    public static string Run(int n)
    {
        var planets = new SystemState();
        double before = planets.Energy();
        for (int i = 0; i < n; i++)
            planets.Advance(0.01);

        double after = planets.Energy();
        return $"nbody {BitConverter.DoubleToUInt64Bits(before):x} {BitConverter.DoubleToUInt64Bits(after):x}\n";
    }
}
