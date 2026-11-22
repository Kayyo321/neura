internal static class Dispatch
{
    private const int Shapes = 1000;
    private const long Mask = 1073741823L;

    private interface IShape
    {
        long Weight();
        long Mix(long value);
    }

    private class Square(long length) : IShape
    {
        protected readonly long Side = length;

        public virtual long Weight() => Side * Side;
        public long Mix(long value) => value + Side;
    }

    private sealed class Cube(long length) : Square(length)
    {
        public override long Weight() => Side * Side * Side;
    }

    private sealed class Rectangle(long width, long height) : IShape
    {
        public long Weight() => width * height;
        public long Mix(long value) => value ^ width;
    }

    private sealed class Triangle(long width, long height) : IShape
    {
        public long Weight() => width * height / 2;
        public long Mix(long value) => value * 3 + height;
    }

    private sealed class Circle(long radius) : IShape
    {
        public long Weight() => 3 * radius * radius;
        public long Mix(long value) => (value >> 1) + radius;
    }

    public static string Run(int n)
    {
        var shapes = new List<IShape>();
        long seed = 12345;
        for (int i = 0; i < Shapes; i++)
        {
            seed = (seed * 1103515245L + 12345) & 2147483647L;
            long kind = seed % 5;
            long a = (seed >> 8) % 100 + 1;
            long b = (seed >> 16) % 100 + 1;
            IShape shape = kind switch
            {
                0 => new Square(a),
                1 => new Cube(a),
                2 => new Rectangle(a, b),
                3 => new Triangle(a, b),
                _ => new Circle(a),
            };
            shapes.Add(shape);
        }
        long checksum = 0;
        for (int round = 0; round < n; round++)
            for (int i = 0; i < shapes.Count; i++)
            {
                IShape shape = shapes[i];
                checksum = (checksum * 31 + shape.Weight() + shape.Mix(checksum & 1023)) & Mask;
            }

        return $"dispatch {shapes.Count} {checksum}\n";
    }
}
