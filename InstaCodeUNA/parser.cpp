#include "parser.h"

#include <QStringList>
#include <QRegularExpression>

QString Parser::convert(const QString &input) {
    QString output;
    QStringList lines = input.split('\n');  // Divide el texto por líneas

    for (const QString &line : lines) {
        QString l = line.trimmed().toLower();

        if (l.startsWith("sumar")) {
            // Ejemplo: "sumar 5 y 3"
            QStringList partes = l.split(" ");
            if (partes.size() >= 4) {
                QString num1 = partes[1];
                QString num2 = partes[3];
                output += "int resultado = " + num1 + " + " + num2 + ";\n";
            } else {
                output += "// No se pudo interpretar: " + line + "\n";
            }
        } else if (l.contains("crear") && l.contains("lista") && l.contains("elementos")) {
            // Ejemplo: "crear una lista de 10 elementos"
            QRegularExpression re("(\\d+)");
            QRegularExpressionMatch match = re.match(l);
            if (match.hasMatch()) {
                QString cantidad = match.captured(1);
                output += "int lista[" + cantidad + "];\n";
            } else {
                output += "// No se encontró cantidad para la lista\n";
            }
        } else {
            output += "// Instrucción no reconocida: " + line + "\n";
        }
    }

    return output;
}
