// Same algorithm, input, and output as ../neura/dispatch.nra: an interface,
// four classes that implement it, and one class that extends another and
// overrides a method, called through the interface.
import java.util.ArrayList;

public final class Dispatch {
    private static final int SHAPES = 1000;
    private static final long MASK = 1073741823L;

    private interface Shape {
        long weight();

        long mix(long value);
    }

    private static class Square implements Shape {
        final long side;

        Square(long length) {
            side = length;
        }

        public long weight() {
            return side * side;
        }

        public long mix(long value) {
            return value + side;
        }
    }

    private static class Cube extends Square {
        Cube(long length) {
            super(length);
        }

        @Override
        public long weight() {
            return side * side * side;
        }
    }

    private static class Rectangle implements Shape {
        final long width;
        final long height;

        Rectangle(long across, long down) {
            width = across;
            height = down;
        }

        public long weight() {
            return width * height;
        }

        public long mix(long value) {
            return value ^ width;
        }
    }

    private static class Triangle implements Shape {
        final long base;
        final long height;

        Triangle(long across, long down) {
            base = across;
            height = down;
        }

        public long weight() {
            return base * height / 2;
        }

        public long mix(long value) {
            return value * 3 + height;
        }
    }

    private static class Circle implements Shape {
        final long radius;

        Circle(long length) {
            radius = length;
        }

        public long weight() {
            return 3 * radius * radius;
        }

        public long mix(long value) {
            return (value >> 1) + radius;
        }
    }

    public static void main(String[] args) {
        int n = Integer.parseInt(args[0]);
        ArrayList<Shape> shapes = new ArrayList<>();
        long seed = 12345;
        for (int i = 0; i < SHAPES; i = i + 1) {
            seed = (seed * 1103515245L + 12345) & 2147483647L;
            long kind = seed % 5;
            long a = (seed >> 8) % 100 + 1;
            long b = (seed >> 16) % 100 + 1;
            if (kind == 0) {
                shapes.add(new Square(a));
            } else if (kind == 1) {
                shapes.add(new Cube(a));
            } else if (kind == 2) {
                shapes.add(new Rectangle(a, b));
            } else if (kind == 3) {
                shapes.add(new Triangle(a, b));
            } else {
                shapes.add(new Circle(a));
            }
        }
        long checksum = 0;
        for (int round = 0; round < n; round = round + 1) {
            for (int i = 0; i < shapes.size(); i = i + 1) {
                Shape shape = shapes.get(i);
                checksum = (checksum * 31 + shape.weight() + shape.mix(checksum & 1023)) & MASK;
            }
        }
        System.out.println("dispatch " + shapes.size() + " " + checksum);
    }
}
