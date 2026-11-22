import com.sun.source.tree.BinaryTree;
import com.sun.source.tree.CompilationUnitTree;
import com.sun.source.tree.MethodTree;
import com.sun.source.tree.Tree;
import com.sun.source.tree.UnaryTree;
import com.sun.source.util.JavacTask;
import com.sun.source.util.SourcePositions;
import com.sun.source.util.TreePathScanner;
import com.sun.source.util.Trees;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import javax.lang.model.type.TypeKind;
import javax.tools.Diagnostic;
import javax.tools.DiagnosticCollector;
import javax.tools.JavaFileObject;
import javax.tools.ToolProvider;

/**
 * Generates checked-arithmetic copies of the Java benchmarks without adding a
 * mode check to the measured code. javac supplies the promoted operator type
 * and the source spans, so float and String operators stay as written.
 */
public final class CheckedSources {
    private record Edit(int start, int end, String text) {}

    private static final class Rewrite extends TreePathScanner<Void, Void> {
        private final Trees trees;
        private final SourcePositions positions;
        private final CompilationUnitTree unit;
        private final List<Edit> edits = new ArrayList<>();

        Rewrite(Trees trees, CompilationUnitTree unit) {
            this.trees = trees;
            this.positions = trees.getSourcePositions();
            this.unit = unit;
        }

        private int start(Tree tree) {
            return Math.toIntExact(positions.getStartPosition(unit, tree));
        }

        private int end(Tree tree) {
            return Math.toIntExact(positions.getEndPosition(unit, tree));
        }

        private boolean fixedWidthInteger() {
            TypeKind kind = trees.getTypeMirror(getCurrentPath()).getKind();
            return kind == TypeKind.INT || kind == TypeKind.LONG;
        }

        @Override
        public Void visitMethod(MethodTree tree, Void unused) {
            // Some algorithms, such as SHA-256 word arithmetic, explicitly
            // wrap. Keep the isolated operation as written in both builds.
            if (tree.getModifiers().getAnnotations().stream()
                    .anyMatch(annotation -> annotation.getAnnotationType().toString().equals("Wrapping"))) {
                return null;
            }
            return super.visitMethod(tree, unused);
        }

        @Override
        public Void visitBinary(BinaryTree tree, Void unused) {
            String method = switch (tree.getKind()) {
                case PLUS -> "addExact";
                case MINUS -> "subtractExact";
                case MULTIPLY -> "multiplyExact";
                case DIVIDE -> "divideExact";
                default -> null;
            };
            if (method != null && fixedWidthInteger()) {
                edits.add(new Edit(start(tree), start(tree), "Math." + method + "("));
                edits.add(new Edit(end(tree.getLeftOperand()), start(tree.getRightOperand()), ", "));
                edits.add(new Edit(end(tree), end(tree), ")"));
            }
            return super.visitBinary(tree, unused);
        }

        @Override
        public Void visitUnary(UnaryTree tree, Void unused) {
            if (tree.getKind() == Tree.Kind.UNARY_MINUS && fixedWidthInteger()) {
                edits.add(new Edit(start(tree), start(tree.getExpression()), "Math.negateExact("));
                edits.add(new Edit(end(tree), end(tree), ")"));
            }
            return super.visitUnary(tree, unused);
        }

        String source() throws IOException {
            scan(unit, null);
            String input = unit.getSourceFile().getCharContent(false).toString();
            StringBuilder output = new StringBuilder(input.length());
            // Stable order keeps enclosing calls outside the opening
            // parentheses of their operands.
            edits.sort(Comparator.comparingInt(Edit::start).thenComparingInt(Edit::end));
            int cursor = 0;
            for (Edit edit : edits) {
                output.append(input, cursor, edit.start()).append(edit.text());
                cursor = edit.end();
            }
            output.append(input, cursor, input.length());
            return output.toString();
        }
    }

    public static void main(String[] args) throws IOException {
        if (args.length != 2) {
            throw new IllegalArgumentException("Usage: java CheckedSources.java <source-dir> <output-dir>");
        }
        Path source = Path.of(args[0]);
        Path output = Path.of(args[1]);
        Files.createDirectories(output);
        List<Path> files;
        try (var entries = Files.list(source)) {
            files = entries.filter(path -> path.toString().endsWith(".java")).sorted().toList();
        }
        var compiler = ToolProvider.getSystemJavaCompiler();
        var diagnostics = new DiagnosticCollector<JavaFileObject>();
        try (var manager = compiler.getStandardFileManager(diagnostics, null, StandardCharsets.UTF_8)) {
            var task = (JavacTask) compiler.getTask(null, manager, diagnostics, List.of("-proc:none"),
                null, manager.getJavaFileObjectsFromPaths(files));
            var units = task.parse();
            task.analyze();
            for (var diagnostic : diagnostics.getDiagnostics()) {
                if (diagnostic.getKind() == Diagnostic.Kind.ERROR)
                    throw new IllegalArgumentException(diagnostic.toString());
            }
            for (var unit : units) {
                Path name = Path.of(unit.getSourceFile().toUri()).getFileName();
                Files.writeString(output.resolve(name), new Rewrite(Trees.instance(task), unit).source());
            }
        }
    }
}
