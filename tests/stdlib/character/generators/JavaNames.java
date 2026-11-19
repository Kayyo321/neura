// Prints Character.getName for every code point that has one, as "HEX<TAB>name" lines, for the cross-check
// of tests/stdlib/character/generators/names.py:
//
//     java JavaNames.java > java-names.txt
//
// Any Java 11 or later runs a single source file directly. Its Unicode version is the JDK's (13.0 for Java 17),
// so code points assigned later have no name here.
public class JavaNames {
    public static void main(String[] args) {
        StringBuilder out = new StringBuilder();
        for (int point = 0; point <= Character.MAX_CODE_POINT; point++) {
            String name = Character.getName(point);
            if (name != null) {
                out.append(Integer.toHexString(point).toUpperCase()).append('\t').append(name).append('\n');
            }
        }
        System.out.print(out);
        System.out.flush();
    }
}
