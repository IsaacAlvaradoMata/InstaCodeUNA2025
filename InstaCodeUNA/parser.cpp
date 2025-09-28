#include "parser.h"

#include <QChar>
#include <QMap>
#include <QPair>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QTextStream>
#include <QVector>
#include <QtGlobal>
#include <algorithm>

namespace {

QString removeDiacritics(const QString &text) {
    QString decomposed = text.normalized(QString::NormalizationForm_D);
    QString result;
    result.reserve(decomposed.size());
    for (const QChar &ch : decomposed) {
        if (ch.category() != QChar::Mark_NonSpacing &&
            ch.category() != QChar::Mark_SpacingCombining &&
            ch.category() != QChar::Mark_Enclosing) {
            result.append(ch);
        }
    }
    return result;
}

QString normalizeLine(const QString &line) {
    QString simplified = removeDiacritics(line).toLower();
    simplified.replace('\r', ' ');
    simplified.replace('\n', ' ');
    simplified.replace(QRegularExpression("[\\s]+"), " ");
    return simplified.trimmed();
}

QString sanitizedIdentifier(const QString &source) {
    QString ascii = removeDiacritics(source).toLower();
    QString result;
    bool lastWasUnderscore = false;
    for (const QChar &ch : ascii) {
        if (ch.isLetterOrNumber()) {
            result.append(ch);
            lastWasUnderscore = false;
        } else if (!lastWasUnderscore) {
            if (!result.isEmpty()) {
                result.append('_');
            }
            lastWasUnderscore = true;
        }
    }
    while (!result.isEmpty() && result.endsWith('_')) {
        result.chop(1);
    }
    if (result.isEmpty()) {
        result = QStringLiteral("valor");
    }
    if (result.front().isDigit()) {
        result.prepend('v');
    }
    return result;
}

QString escapeForStringLiteral(const QString &text) {
    QString escaped;
    escaped.reserve(text.size() + 8);
    for (const QChar &ch : text) {
        if (ch == '"' || ch == '\\') {
            escaped.append('\\');
            escaped.append(ch);
        } else if (ch == '\n') {
            escaped.append("\\n");
        } else if (ch == '\r') {
            continue;
        } else {
            escaped.append(ch);
        }
    }
    return escaped;
}

QString quoted(const QString &text) {
    return QStringLiteral("\"") + escapeForStringLiteral(text) + QStringLiteral("\"");
}

QString ensureNumberString(const QString &value, bool floating) {
    QString cleaned = value.trimmed();
    if (cleaned.isEmpty()) {
        return floating ? QStringLiteral("0.0") : QStringLiteral("0");
    }
    cleaned.replace(',', '.');
    if (floating && !cleaned.contains('.')) {
        cleaned.append(".0");
    }
    return cleaned;
}

QString readQuotedText(const QString &line) {
    int first = line.indexOf('"');
    if (first < 0) {
        return QString();
    }
    int second = line.indexOf('"', first + 1);
    if (second < 0) {
        return QString();
    }
    return line.mid(first + 1, second - first - 1);
}
QStringList splitLines(const QString &text) {
    return text.split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts);
}

enum class BlockType { Generic, If, Loop };

struct BlockState {
    BlockType type = BlockType::Generic;
    bool autoClose = false; // true: close before next non-else instruction
    bool hasElse = false;
    int indent = 0;
};

struct VariableInfo {
    QString type;
    bool fromInstruction = false;
};

struct CollectionInfo {
    QString type;            // e.g. std::vector<int>
    QString elementType;     // e.g. int
    QString alias;           // textual reference, e.g. "lista", "vector"
    int size = 0;
    bool fixedSize = false;
    bool isCArray = false;
};

class InstructionParser {
public:
    explicit InstructionParser(const Parser::Input &input)
        : m_input(input) {
        ensureInclude("iostream");
        m_dataFileName = input.dataFileName.trimmed();
        if (m_dataFileName.isEmpty()) {
            m_dataFileName = QStringLiteral("datos.txt");
        }
    }

    Parser::Output run() {
        const QStringList lines = splitLines(m_input.instructions);
        for (const QString &rawLine : lines) {
            QString trimmed = rawLine.trimmed();
            if (trimmed.isEmpty()) {
                continue;
            }

            int leadingSpaces = rawLine.indexOf(trimmed);
            if (leadingSpaces < 0) leadingSpaces = 0;
            QString normalized = normalizeLine(trimmed);

            bool isSino = normalized.startsWith(QStringLiteral("sino"));
            if (!isSino) {
                closeAutoBlocks(leadingSpaces);
            }

            m_currentIndent = leadingSpaces;

            if (!processLine(trimmed, normalized)) {
                m_success = false;
                m_issues.append(QStringLiteral("Instrucción no reconocida: %1").arg(trimmed));
            }
        }

        closeAutoBlocks(0);
        closeAllBlocks();

        if (!m_dataWritten && !m_input.dataFileContents.trimmed().isEmpty() && m_requiresDataFile) {
            // Data requested but not written (e.g. no lectura line matched)
            m_success = false;
            m_issues.append(QStringLiteral("Se cargó un archivo de datos pero ninguna instrucción lo utilizó."));
        }

        QStringList output;
        QStringList includeList = m_includes.values();
        std::sort(includeList.begin(), includeList.end());
        for (const QString &inc : includeList) {
            output << QStringLiteral("#include <%1>").arg(inc);
        }
        output << QString();
        output << QString();
        output << QStringLiteral("int main() {");

        if (!m_startupLines.isEmpty()) {
            for (const auto &line : m_startupLines) {
                output << QStringLiteral("%1%2").arg(QStringLiteral("    ").repeated(line.first), line.second);
            }
            if (!m_codeLines.isEmpty()) {
                output << QString();
            }
        }

        for (const QString &line : m_codeLines) {
            output << line;
        }

        output << QStringLiteral("    return 0;");
        output << QStringLiteral("}");

        Parser::Output result;
        result.code = output.join('\n');
        result.issues = m_issues;
        result.success = m_success;
        return result;
    }

private:
    bool processLine(const QString &original, const QString &normalized) {
        if (normalized.isEmpty()) {
            return true;
        }

        QString core = normalized;
        if (core.endsWith('.')) {
            core.chop(1);
        }

        if (core == QStringLiteral("comenzar programa") ||
            core == QStringLiteral("terminar programa")) {
            return true;
        }

        if (core.startsWith(QStringLiteral("sino"))) {
            return handleElse(original, core);
        }

        if (handleCreateVariable(original, core)) return true;
        if (handleAssignValue(original, core)) return true;
        if (handleCalculateExpression(original, core)) return true;
        if (handleUserInput(original, core)) return true;
        if (handleInputValue(original, core)) return true;
        if (handleArithmeticBinary(original, core)) return true;
        if (handleArithmeticAggregate(original, core)) return true;
        if (handleRepeatMessage(original, core)) return true;
        if (handleWhileIncrease(original, core)) return true;
        if (handleCreateCollection(original, core)) return true;
        if (handleAssignCollectionElement(original, core)) return true;
        if (handleIterateCollectionSum(original, core)) return true;
        if (handleAddToCollection(original, core)) return true;
        if (handleRemoveFromCollection(original, core)) return true;
        if (handleSortCollection(original, core)) return true;
        if (handleIterateCollection(original, core)) return true;
        if (handleIfCondition(original, core)) return true;
        if (handleShowMessage(original, core)) return true;
        if (handleUserInput(original, core)) return true;
        if (handlePrintCollection(original, core)) return true;
        if (handleReadDataFile(original, core)) return true;
        if (handlePrintPairs(original, core)) return true;
        if (core.startsWith(QStringLiteral("guardar los numeros en"))) {
            return true; // Instrución orgánica sin código explícito
        }

        return false;
    }

    void ensureInclude(const QString &include) {
        m_includes.insert(include);
    }

    QString indent() const {
        return QStringLiteral("    ").repeated(m_indentLevel);
    }

    void addCodeLine(const QString &line) {
        m_codeLines.append(indent() + line);
    }

    void notifyIssue(const QString &message) {
        m_issues.append(message);
    }

    void addStartupLine(const QString &line, int indentLevel = 1) {
        m_startupLines.append({indentLevel, line});
    }

    void startBlock(const QString &header, BlockType type, bool autoClose = false, int indentLevel = 0) {
        m_codeLines.append(indent() + header);
        m_blocks.push_back({type, autoClose, false, indentLevel});
        ++m_indentLevel;
    }

    void endBlock() {
        if (m_indentLevel > 1) {
            --m_indentLevel;
        }
        m_codeLines.append(indent() + QStringLiteral("}"));
        if (!m_blocks.isEmpty()) {
            m_blocks.removeLast();
        }
    }

    void closeAutoBlocks(int currentIndent) {
        while (!m_blocks.isEmpty()) {
            auto &block = m_blocks.last();
            if (!block.autoClose) {
                break;
            }
            if (block.hasElse && currentIndent > block.indent) {
                break;
            }
            if (currentIndent > block.indent) {
                break;
            }
            endBlock();
        }
    }

    void closeAllBlocks() {
        while (!m_blocks.isEmpty()) {
            endBlock();
        }
    }

    void registerVariable(const QString &name, const QString &type, bool byInstruction = true) {
        m_variables.insert(name, {type, byInstruction});
    }

    bool hasVariable(const QString &name) const {
        return m_variables.contains(name);
    }

    QString variableType(const QString &name) const {
        return m_variables.value(name).type;
    }

    void ensureVariable(const QString &name, const QString &type, const QString &initializer) {
        if (hasVariable(name)) {
            return;
        }
        addCodeLine(QStringLiteral("%1 %2 = %3;").arg(type, name, initializer));
        registerVariable(name, type, false);
    }

    QString getUniqueVariableName(const QString &baseName) {
        QString candidate = baseName;
        int suffix = 1;
        while (hasVariable(candidate) || m_collections.contains(candidate)) {
            candidate = baseName + QString::number(suffix++);
        }
        return candidate;
    }

    void registerCollection(const QString &name, const CollectionInfo &info) {
        m_collections.insert(name, info);
        m_lastCollection = name;
    }

    bool hasCollection(const QString &name) const {
        return m_collections.contains(name);
    }

    QString collectionNameForAlias(const QString &alias) const {
        QString key = sanitizedIdentifier(alias);
        for (auto it = m_collections.constBegin(); it != m_collections.constEnd(); ++it) {
            if (it.value().alias == key) {
                return it.key();
            }
        }
        return QString();
    }

    QString lastCollection() const {
        if (!m_lastCollection.isEmpty()) {
            return m_lastCollection;
        }
        if (!m_collections.isEmpty()) {
            return m_collections.constBegin().key();
        }
        return QString();
    }

    QString elementTypeForCollection(const QString &name) const {
        return m_collections.value(name).elementType;
    }

    int collectionSize(const QString &name) const {
        return m_collections.value(name).size;
    }

    bool isCollectionFixedSize(const QString &name) const {
        return m_collections.value(name).fixedSize;
    }

    bool isCollectionCArray(const QString &name) const {
        return m_collections.value(name).isCArray;
    }

    bool handleCreateVariable(const QString &original, const QString &normalized) {
        if (!normalized.startsWith(QStringLiteral("crear variable"))) {
            return false;
        }

        QString rest = normalized.mid(QStringLiteral("crear variable").size()).trimmed();
        QString typeToken;
        QString nameToken;
        QString valueToken;

        const struct {
            QString key;
            QString type;
            bool floating;
        } typeMap[] = {
            {QStringLiteral("numero decimal"), QStringLiteral("double"), true},
            {QStringLiteral("numero entero"), QStringLiteral("int"), false},
            {QStringLiteral("texto"), QStringLiteral("std::string"), false},
            {QStringLiteral("cadena"), QStringLiteral("std::string"), false},
            {QStringLiteral("booleano"), QStringLiteral("bool"), false}
        };

        QString chosenType;
        bool isFloating = false;
        for (const auto &entry : typeMap) {
            if (rest.startsWith(entry.key)) {
                chosenType = entry.type;
                isFloating = entry.floating;
                typeToken = entry.key;
                break;
            }
        }

        if (chosenType.isEmpty()) {
            return false;
        }

        QString afterType = rest.mid(typeToken.size()).trimmed();
        if (afterType.isEmpty()) {
            return false;
        }

        int valueIdx = afterType.indexOf(QStringLiteral("con valor inicial"));
        if (valueIdx >= 0) {
            nameToken = afterType.left(valueIdx).trimmed();
            valueToken = afterType.mid(valueIdx + QStringLiteral("con valor inicial").size()).trimmed();
        } else {
            nameToken = afterType.trimmed();
        }

        if (nameToken.isEmpty()) {
            nameToken = QStringLiteral("variable");
        }

        QString identifier = sanitizedIdentifier(nameToken);

        QString initializer;
        if (!valueToken.isEmpty()) {
            if (chosenType == QStringLiteral("std::string")) {
                QString quotedText = readQuotedText(original);
                if (quotedText.isEmpty()) {
                    initializer = quoted(valueToken);
                } else {
                    initializer = quoted(quotedText);
                }
                ensureInclude("string");
            } else if (chosenType == QStringLiteral("bool")) {
                if (valueToken.contains(QStringLiteral("verdadero"))) {
                    initializer = QStringLiteral("true");
                } else if (valueToken.contains(QStringLiteral("falso"))) {
                    initializer = QStringLiteral("false");
                } else {
                    initializer = QStringLiteral("false");
                }
            } else {
                initializer = ensureNumberString(valueToken, isFloating);
            }
        } else {
            if (chosenType == QStringLiteral("std::string")) {
                initializer = QStringLiteral("\"\"");
                ensureInclude("string");
            } else if (chosenType == QStringLiteral("bool")) {
                initializer = QStringLiteral("false");
            } else {
                initializer = isFloating ? QStringLiteral("0.0") : QStringLiteral("0");
            }
        }

        addCodeLine(QStringLiteral("%1 %2 = %3;").arg(chosenType, identifier, initializer));
        registerVariable(identifier, chosenType);
        return true;
    }

    bool handleAssignValue(const QString &original, const QString &normalized) {
        if (!normalized.startsWith(QStringLiteral("asignar valor")) &&
            !normalized.startsWith(QStringLiteral("asignar"))) {
            return false;
        }

        QString rest;
        if (normalized.startsWith(QStringLiteral("asignar valor"))) {
            rest = normalized.mid(QStringLiteral("asignar valor").size()).trimmed();
        } else {
            rest = normalized.mid(QStringLiteral("asignar").size()).trimmed();
        }

        int toIdx = rest.lastIndexOf(QStringLiteral(" a "));
        if (toIdx < 0) {
            toIdx = rest.lastIndexOf(QStringLiteral(" al "));
        }
        if (toIdx < 0) {
            return false;
        }

        QString valuePart = rest.left(toIdx).trimmed();
        QString namePart = rest.mid(toIdx).trimmed();

        if (namePart.startsWith(QStringLiteral("a "))) {
            namePart = namePart.mid(2).trimmed();
        }
        if (namePart.startsWith(QStringLiteral("al "))) {
            namePart = namePart.mid(3).trimmed();
        }

        QString identifier = sanitizedIdentifier(namePart);
        if (!hasVariable(identifier)) {
            // Try to determine type from value
            QString varType = QStringLiteral("double");
            if (valuePart == QStringLiteral("verdadero") || 
                valuePart == QStringLiteral("falso") ||
                valuePart == QStringLiteral("true") || 
                valuePart == QStringLiteral("false")) {
                varType = QStringLiteral("bool");
            }
            QString initialValue = (varType == QStringLiteral("bool")) ? QStringLiteral("false") : QStringLiteral("0.0");
            ensureVariable(identifier, varType, initialValue);
        }

        QString valueExpr = translateExpression(valuePart, original);
        addCodeLine(QStringLiteral("%1 = %2;").arg(identifier, valueExpr));
        return true;
    }


    bool handleCalculateExpression(const QString &original, const QString &normalized) {
        if (!normalized.startsWith(QStringLiteral("calcular "))) {
            return false;
        }

        QString rest = normalized.mid(QStringLiteral("calcular ").size()).trimmed();
        int idxComo = rest.indexOf(QStringLiteral(" como "));
        int idxAsignar = rest.indexOf(QStringLiteral(" y asignar a "));
        int idxAsignarAl = rest.indexOf(QStringLiteral(" y asignar al "));

        QString assignToken;
        if (idxAsignar >= 0) {
            assignToken = QStringLiteral(" y asignar a ");
        } else if (idxAsignarAl >= 0) {
            assignToken = QStringLiteral(" y asignar al ");
            idxAsignar = idxAsignarAl;
        } else {
            return false;
        }

        if (idxComo < 0 || idxAsignar <= idxComo) {
            return false;
        }

        QString exprPart = rest.mid(idxComo + QStringLiteral(" como ").size(), idxAsignar - (idxComo + QStringLiteral(" como ").size())).trimmed();
        QString destPart = rest.mid(idxAsignar + assignToken.size()).trimmed();
        if (destPart.isEmpty()) {
            return false;
        }

        QString dest = sanitizedIdentifier(destPart);
        if (dest.isEmpty()) {
            notifyIssue(QStringLiteral("No se pudo interpretar la variable destino en la instrucción de cálculo."));
            return true;
        }

        QString expr = translateExpression(exprPart, original);
        if (expr.isEmpty()) {
            notifyIssue(QStringLiteral("No se pudo interpretar la expresión a calcular: %1").arg(exprPart));
            return true;
        }

        if (!hasVariable(dest)) {
            ensureVariable(dest, QStringLiteral("double"), QStringLiteral("0.0"));
        }

        addCodeLine(QStringLiteral("%1 = %2;").arg(dest, expr));
        return true;
    }

    bool handleInputValue(const QString &original, const QString &normalized) {
        Q_UNUSED(original);
        QString core;
        if (normalized.startsWith(QStringLiteral("ingresar valor"))) {
            core = normalized.mid(QStringLiteral("ingresar valor").size()).trimmed();
        } else if (normalized.startsWith(QStringLiteral("ingresar los valores"))) {
            core = normalized.mid(QStringLiteral("ingresar los valores").size()).trimmed();
        } else if (normalized.startsWith(QStringLiteral("ingresar"))) {
            core = normalized.mid(QStringLiteral("ingresar").size()).trimmed();
        } else {
            return false;
        }

        if (core.isEmpty()) {
            notifyIssue(QStringLiteral("Se solicitó ingresar un valor, pero no se indicó la variable."));
            return true;
        }

        // Handle "de la lista", "del vector", "de la arreglo"
        if (core.startsWith(QStringLiteral("de la ")) || core.startsWith(QStringLiteral("del "))) {
            QString remainder = core.startsWith(QStringLiteral("de la ")) ? 
                               core.mid(QStringLiteral("de la ").size()).trimmed() :
                               core.mid(QStringLiteral("del ").size()).trimmed();
            
            if (remainder == QStringLiteral("lista") || 
                remainder == QStringLiteral("vector") || 
                remainder == QStringLiteral("arreglo")) {
                return requestInputForCollection(remainder);
            }
        }

        QRegularExpression eachElementRe(QStringLiteral("^de cada (.+) en (?:la|el) (lista|vector|arreglo)$"));
        QRegularExpressionMatch eachElementMatch = eachElementRe.match(core);
        if (eachElementMatch.hasMatch()) {
            QString alias = eachElementMatch.captured(2);
            return requestInputForCollection(alias);
        }

        QString identifier = sanitizedIdentifier(core);
        if (identifier.isEmpty()) {
            notifyIssue(QStringLiteral("No se pudo interpretar la variable para ingresar datos."));
            return true;
        }

        if (!hasVariable(identifier)) {
            notifyIssue(QStringLiteral("La variable %1 no ha sido creada antes de ingresar un valor.").arg(identifier));
            return true;
        }

        ensureInclude("iostream");
        addCodeLine(QStringLiteral("std::cin >> %1;").arg(identifier));
        return true;
    }

    QString translateExpression(const QString &valuePart, const QString &original) {
        QString expr = valuePart;
        QString normalizedExpr = normalizeLine(expr);

        // Handle boolean values first
        if (normalizedExpr == QStringLiteral("verdadero") || normalizedExpr == QStringLiteral("true")) {
            return QStringLiteral("true");
        }
        if (normalizedExpr == QStringLiteral("falso") || normalizedExpr == QStringLiteral("false")) {
            return QStringLiteral("false");
        }

        normalizedExpr.replace(QStringLiteral(" mas "), QStringLiteral(" + "));
        normalizedExpr.replace(QStringLiteral(" mas"), QStringLiteral(" +"));
        normalizedExpr.replace(QStringLiteral("menos"), QStringLiteral("-"));
        normalizedExpr.replace(QStringLiteral(" multiplicado por "), QStringLiteral(" * "));
        normalizedExpr.replace(QStringLiteral(" dividido entre "), QStringLiteral(" / "));

        if (normalizedExpr.contains(QStringLiteral("total dividido entre"))) {
            QRegularExpression re(QString::fromLatin1(R"(([a-zA-Z_][a-zA-Z0-9_]*)\s+dividido entre\s+(-?\d+(?:[.,]\d+)?))"));
            QRegularExpressionMatch match = re.match(normalizedExpr);
            if (match.hasMatch()) {
                QString varName = sanitizedIdentifier(match.captured(1));
                QString number = ensureNumberString(match.captured(2), true);
                return QStringLiteral("%1 / %2").arg(varName, number);
            }
        }

        if (normalizedExpr.contains(QStringLiteral("total")) && normalizedExpr.contains(QStringLiteral(" entre "))) {
            QStringList parts = normalizedExpr.split(QStringLiteral(" entre "));
            if (parts.size() == 2) {
                QString left = sanitizedIdentifier(parts[0].trimmed());
                QString right = ensureNumberString(parts[1], true);
                return QStringLiteral("%1 / %2").arg(left, right);
            }
        }

        QRegularExpression simpleDivide(QStringLiteral("^([a-zA-Z_][a-zA-Z0-9_]*)\\s+dividir\\s+(.+)$"));
        QRegularExpressionMatch simpleMatch = simpleDivide.match(normalizedExpr);
        if (simpleMatch.hasMatch()) {
            QString left = sanitizedIdentifier(simpleMatch.captured(1));
            QString rightToken = simpleMatch.captured(2).trimmed();
            QString right;
            if (QRegularExpression(QStringLiteral("^-?\\d+(?:[\\.,]\\d+)?$"))
                    .match(rightToken).hasMatch()) {
                right = ensureNumberString(rightToken, rightToken.contains('.') || rightToken.contains(','));
            } else {
                right = sanitizedIdentifier(rightToken);
            }
            if (left.isEmpty() || right.isEmpty()) {
                return QString();
            }
            return QStringLiteral("%1 / %2").arg(left, right);
        }

        if (normalizedExpr.contains(QStringLiteral(" dividido entre "))) {
            QStringList parts = normalizedExpr.split(QStringLiteral(" dividido entre "));
            if (parts.size() == 2) {
                QString left = translateExpression(parts[0].trimmed(), original);
                QString right = ensureNumberString(parts[1], true);
                return QStringLiteral("%1 / %2").arg(left, right);
            }
        }

        QRegularExpression numbers("-?\\d+(?:[\\.,]\\d+)?");
        QString processed = normalizedExpr;
        int idx = 0;
        while (true) {
            QRegularExpressionMatch match = numbers.match(processed, idx);
            if (!match.hasMatch()) {
                break;
            }
            QString number = match.captured();
            QString replacement = ensureNumberString(number, number.contains('.') || number.contains(','));
            processed.replace(match.capturedStart(), match.capturedLength(), replacement);
            idx = match.capturedStart() + replacement.size();
        }

        processed.replace(QStringLiteral(" "), QString());
        return processed;
    }

    QString literalForType(const QString &valueText,
                           const QString &original,
                           const QString &type)
    {
        QString trimmed = valueText.trimmed();

        if (type == QStringLiteral("std::string")) {
            ensureInclude("string");
            QString quotedText = readQuotedText(original);
            if (quotedText.isEmpty()) {
                quotedText = trimmed;
            }
            return quoted(quotedText);
        }
        
        if (type == QStringLiteral("bool")) {
            if (trimmed == QStringLiteral("verdadero") || trimmed == QStringLiteral("true")) {
                return QStringLiteral("true");
            }
            if (trimmed == QStringLiteral("falso") || trimmed == QStringLiteral("false")) {
                return QStringLiteral("false");
            }
            QString identifier = sanitizedIdentifier(trimmed);
            if (hasVariable(identifier)) {
                return identifier;
            }
            return QStringLiteral("false");
        }

        bool hasDecimal = trimmed.contains('.') || trimmed.contains(',');
        bool expectFloating = (type == QStringLiteral("double")) || hasDecimal;
        if (type == QStringLiteral("double") || type == QStringLiteral("int")) {
            QRegularExpression numberRegex(QStringLiteral(R"(-?\d+(?:[.,]\d+)?)"));
            QRegularExpressionMatch match = numberRegex.match(trimmed);
            if (match.hasMatch()) {
                return ensureNumberString(match.captured(), expectFloating);
            }
            QString identifier = sanitizedIdentifier(trimmed);
            if (!identifier.isEmpty()) {
                return identifier;
            }
            notifyIssue(QStringLiteral("No se pudo interpretar el valor numérico: %1").arg(valueText));
            return expectFloating ? QStringLiteral("0.0") : QStringLiteral("0");
        }

        QString identifier = sanitizedIdentifier(trimmed);
        if (identifier.isEmpty()) {
            notifyIssue(QStringLiteral("No se pudo interpretar el valor '%1' para la colección").arg(valueText));
        }
        return identifier.isEmpty() ? trimmed : identifier;
    }

    bool handleArithmeticBinary(const QString &original, const QString &normalized) {
        Q_UNUSED(original);
        static const struct {
            QString verb;
            QString op;
            bool useEntre;
        } entries[] = {
            {QStringLiteral("sumar"), QStringLiteral("+"), false},
            {QStringLiteral("restar"), QStringLiteral("-"), false},
            {QStringLiteral("multiplicar"), QStringLiteral("*"), false},
            {QStringLiteral("dividir"), QStringLiteral("/"), true}
        };

        auto operandInfo = [](const QString &token) {
            QString trimmed = token.trimmed();
            QRegularExpression numberRegex(QStringLiteral(R"(^-?\d+(?:[.,]\d+)?$)"));
            if (numberRegex.match(trimmed).hasMatch()) {
                bool hasDecimal = trimmed.contains('.') || trimmed.contains(',');
                return QPair<QString, bool>(ensureNumberString(trimmed, hasDecimal), hasDecimal);
            }
            QString identifier = sanitizedIdentifier(trimmed);
            return QPair<QString, bool>(identifier, false);
        };

        for (const auto &entry : entries) {
            if (!normalized.startsWith(entry.verb + QLatin1Char(' '))) {
                continue;
            }

            QString tail = normalized.mid(entry.verb.size()).trimmed();
            QString leftToken;
            QString rightToken;

            if (entry.useEntre) {
                int idx = tail.indexOf(QStringLiteral(" entre "));
                if (idx < 0) {
                    continue;
                }
                leftToken = tail.left(idx).trimmed();
                rightToken = tail.mid(idx + QStringLiteral(" entre ").size()).trimmed();
            } else {
                int idx = tail.indexOf(QStringLiteral(" y "));
                if (idx < 0) {
                    continue;
                }
                leftToken = tail.left(idx).trimmed();
                rightToken = tail.mid(idx + QStringLiteral(" y ").size()).trimmed();
            }

            auto leftInfo = operandInfo(leftToken);
            auto rightInfo = operandInfo(rightToken);

            if (leftInfo.first.isEmpty() || rightInfo.first.isEmpty()) {
                notifyIssue(QStringLiteral("No se pudieron interpretar los operandos de la operación."));
                return false;
            }

            bool useDouble = leftInfo.second || rightInfo.second;
            QString type = useDouble ? QStringLiteral("double") : QStringLiteral("int");

            QString tempName = QStringLiteral("resultado%1").arg(m_tempCounter++);
            addCodeLine(QStringLiteral("%1 %2 = %3 %4 %5;").arg(type, tempName, leftInfo.first, entry.op, rightInfo.first));
            addCodeLine(QStringLiteral("std::cout << %1 << std::endl;").arg(tempName));
            ensureInclude("iostream");
            return true;
        }
        return false;
    }

    bool handleArithmeticAggregate(const QString &original, const QString &normalized) {
        Q_UNUSED(original);
        if (!normalized.startsWith(QStringLiteral("sumar los numeros"))) {
            return false;
        }

        QRegularExpression numberRegex(QStringLiteral(R"(-?\d+(?:[.,]\d+)?)"));
        QRegularExpressionMatchIterator it = numberRegex.globalMatch(normalized);
        QVector<QString> numbers;
        bool anyDecimal = false;
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            QString raw = match.captured();
            bool hasDecimal = raw.contains('.') || raw.contains(',');
            numbers.append(ensureNumberString(raw, hasDecimal));
            anyDecimal = anyDecimal || hasDecimal;
        }

        if (numbers.isEmpty()) {
            return false;
        }

        QString type = anyDecimal ? QStringLiteral("double") : QStringLiteral("int");
        QString initial = anyDecimal ? QStringLiteral("0.0") : QStringLiteral("0");
        QString accumulator = QStringLiteral("suma%1").arg(m_tempCounter++);
        addCodeLine(QStringLiteral("%1 %2 = %3;").arg(type, accumulator, initial));
        for (const QString &num : numbers) {
            addCodeLine(QStringLiteral("%1 += %2;").arg(accumulator, num));
        }
        addCodeLine(QStringLiteral("std::cout << %1 << std::endl;").arg(accumulator));
        ensureInclude("iostream");
        return true;
    }

    bool handleRepeatMessage(const QString &original, const QString &normalized) {
        if (!normalized.startsWith(QStringLiteral("repetir"))) {
            return false;
        }

        QRegularExpression re(QString::fromLatin1(R"(^repetir\s+(\d+)\s+veces\s+(mostrar|imprimir))"));
        QRegularExpressionMatch match = re.match(normalized);
        if (!match.hasMatch()) {
            return false;
        }

        QString times = match.captured(1);
        QString message = readQuotedText(original);
        if (message.isEmpty()) {
            QRegularExpression messageRe(QString::fromLatin1(R"(^repetir\s+\d+\s+veces\s+(?:mostrar|imprimir)\s+(?:el mensaje\s+)?(.+)$)"));
            QRegularExpressionMatch messageMatch = messageRe.match(normalized);
            if (messageMatch.hasMatch()) {
                message = messageMatch.captured(1).trimmed();
                if (message.startsWith('"') && message.endsWith('"') && message.size() >= 2) {
                    message = message.mid(1, message.size() - 2);
                }
            }
        }

        if (message.isEmpty()) {
            return false;
        }

        QString literal = quoted(message);
        ensureInclude("iostream");

        QString counter = QStringLiteral("i%1").arg(m_tempCounter++);
        addCodeLine(QStringLiteral("for (int %1 = 0; %1 < %2; ++%1) {").arg(counter, times));
        ++m_indentLevel;
        addCodeLine(QStringLiteral("std::cout << %1 << std::endl;").arg(literal));
        --m_indentLevel;
        addCodeLine(QStringLiteral("}"));
        return true;
    }

    bool handleWhileIncrease(const QString &original, const QString &normalized) {
        Q_UNUSED(original);
        QRegularExpression re("^mientras el ([a-zA-Z_]+) sea menor que (-?\\d+(?:[\\.,]\\d+)?) sumar (-?\\d+(?:[\\.,]\\d+)?) al \\1$");
        QRegularExpressionMatch match = re.match(normalized);
        if (!match.hasMatch()) {
            return false;
        }

        QString variable = sanitizedIdentifier(match.captured(1));
        QString limit = ensureNumberString(match.captured(2), true);
        QString increment = ensureNumberString(match.captured(3), true);

        ensureVariable(variable, QStringLiteral("double"), QStringLiteral("0.0"));

        addCodeLine(QStringLiteral("while (%1 < %2) {").arg(variable, limit));
        ++m_indentLevel;
        addCodeLine(QStringLiteral("%1 += %2;").arg(variable, increment));
        --m_indentLevel;
        addCodeLine(QStringLiteral("}"));
        return true;
    }

    bool handleCreateCollection(const QString &original, const QString &normalized) {
        Q_UNUSED(original);
        if (!normalized.startsWith(QStringLiteral("crear"))) {
            return false;
        }

        // Specific pattern: lista/vector with size
        QRegularExpression reSize("^crear (?:una |un )?(lista|vector|arreglo) de (?:\\d+ )?([a-z ]+) con (\\d+) elementos$");
        QRegularExpressionMatch m = reSize.match(normalized);
        if (m.hasMatch()) {
            QString alias = m.captured(1);
            QString elementPhrase = m.captured(2).trimmed();
            int size = m.captured(3).toInt();
            QString elementType = elementTypeFromPhrase(elementPhrase);
            QString aliasToken = sanitizedIdentifier(alias);
            if (aliasToken.isEmpty()) {
                aliasToken = QStringLiteral("lista");
            }

            if (aliasToken == QStringLiteral("arreglo")) {
                QString variableName = uniqueName(QStringLiteral("arreglo"));
                addCodeLine(QStringLiteral("%1 %2[%3];").arg(elementType, variableName).arg(size));
                registerCollection(variableName, {elementType, elementType, aliasToken, size, true, true});
                return true;
            }

            QString baseName = aliasToken == QStringLiteral("vector") ? QStringLiteral("vector") : QStringLiteral("lista");
            QString variableName = uniqueName(baseName);
            QString type = QStringLiteral("std::vector<%1>").arg(elementType);
            ensureInclude("vector");
            if (elementType == QStringLiteral("std::string")) {
                ensureInclude("string");
            }
            addCodeLine(QStringLiteral("%1 %2(%3);").arg(type, variableName).arg(size));
            registerCollection(variableName, {type, elementType, aliasToken, size, false, false});
            return true;
        }

        QRegularExpression reSizeShort("^crear (?:una |un )?(lista|vector|arreglo) de (\\d+) ([a-z ]+)$");
        QRegularExpressionMatch mShort = reSizeShort.match(normalized);
        if (mShort.hasMatch()) {
            QString alias = mShort.captured(1);
            int size = mShort.captured(2).toInt();
            QString elementPhrase = mShort.captured(3).trimmed();
            QString elementType = elementTypeFromPhrase(elementPhrase);
            QString aliasToken = sanitizedIdentifier(alias);
            if (aliasToken.isEmpty()) {
                aliasToken = QStringLiteral("lista");
            }

            if (aliasToken == QStringLiteral("arreglo")) {
                QString variableName = uniqueName(QStringLiteral("arreglo"));
                addCodeLine(QStringLiteral("%1 %2[%3];").arg(elementType, variableName).arg(size));
                registerCollection(variableName, {elementType, elementType, aliasToken, size, true, true});
                return true;
            }

            QString baseName = aliasToken == QStringLiteral("vector") ? QStringLiteral("vector") : QStringLiteral("lista");
            QString variableName = uniqueName(baseName);
            QString type = QStringLiteral("std::vector<%1>").arg(elementType);
            ensureInclude("vector");
            if (elementType == QStringLiteral("std::string")) {
                ensureInclude("string");
            }
            addCodeLine(QStringLiteral("%1 %2(%3);").arg(type, variableName).arg(size));
            registerCollection(variableName, {type, elementType, aliasToken, size, false, false});
            return true;
        }

        // Pattern: lista texto para guardar los paises
        QRegularExpression reStore("^crear una lista de texto para guardar (los|las) ([a-z ]+)$");
        QRegularExpressionMatch mStore = reStore.match(normalized);
        if (mStore.hasMatch()) {
            QString aliasWord = mStore.captured(2).trimmed();
            QString variableName = sanitizedIdentifier(aliasWord);
            if (variableName.isEmpty()) {
                variableName = QStringLiteral("lista");
            }
            QString aliasToken = sanitizedIdentifier(aliasWord);
            if (aliasToken.isEmpty()) {
                aliasToken = QStringLiteral("lista");
            }
            QString type = QStringLiteral("std::vector<std::string>");
            ensureInclude("vector");
            ensureInclude("string");
            addCodeLine(QStringLiteral("%1 %2;").arg(type, variableName));
            registerCollection(variableName, {type, QStringLiteral("std::string"), aliasToken, 0, false, false});
            return true;
        }

        // Vector con tamaño indicado de números enteros
        QRegularExpression reVector("^crear un vector de ([a-z ]+) con (\\d+) elementos$");
        QRegularExpressionMatch mVec = reVector.match(normalized);
        if (mVec.hasMatch()) {
            QString elementPhrase = mVec.captured(1).trimmed();
            int size = mVec.captured(2).toInt();
            QString elementType = elementTypeFromPhrase(elementPhrase);
            QString variableName = QStringLiteral("vector");
            variableName = uniqueName(variableName);
            QString type = QStringLiteral("std::vector<%1>").arg(elementType);
            ensureInclude("vector");
            if (elementType == QStringLiteral("std::string")) {
                ensureInclude("string");
            }
            addCodeLine(QStringLiteral("%1 %2(%3);").arg(type, variableName).arg(size));
            registerCollection(variableName, {type, elementType, QStringLiteral("vector"), size, false, false});
            return true;
        }

        return false;
    }

    QString uniqueName(const QString &base) {
        QString candidate = base;
        int suffix = 1;
        while (m_variables.contains(candidate) || m_collections.contains(candidate)) {
            candidate = base + QString::number(++suffix);
        }
        return candidate;
    }

    QString elementTypeFromPhrase(const QString &phrase) {
        if (phrase.contains(QStringLiteral("texto"))) {
            ensureInclude("string");
            return QStringLiteral("std::string");
        }
        if (phrase.contains(QStringLiteral("decimal"))) {
            return QStringLiteral("double");
        }
        if (phrase.contains(QStringLiteral("entero"))) {
            return QStringLiteral("int");
        }
        return QStringLiteral("std::string");
    }

    bool handleAssignCollectionElement(const QString &original, const QString &normalized) {
        if (!normalized.startsWith(QStringLiteral("asignar valor"))) {
            return false;
        }

        QRegularExpression re(QStringLiteral("^asignar valor (.+) al (primer|segundo|tercer|cuarto|quinto|sexto|septimo|octavo|noveno|decimo) elemento de la (lista|vector|arreglo)$"));
        QRegularExpressionMatch match = re.match(normalized);
        if (!match.hasMatch()) {
            return false;
        }

        QString value = match.captured(1).trimmed();
        QString ordinal = match.captured(2);
        QString alias = match.captured(3);

        QString collectionName = collectionNameForAlias(alias);
        if (collectionName.isEmpty()) {
            collectionName = lastCollection();
        }
        if (collectionName.isEmpty()) {
            notifyIssue(QStringLiteral("No se encontró ninguna colección disponible para esta instrucción."));
            return false;
        }

        int index = ordinalToIndex(ordinal);
        QString elementType = elementTypeForCollection(collectionName);
        const auto info = m_collections.value(collectionName);
        if (info.isCArray) {
            if (index < 0 || index >= info.size) {
                notifyIssue(QStringLiteral("El índice indicado está fuera del rango del arreglo."));
                return true;
            }
            QString valueExpr = literalForType(value, original, elementType);
            addCodeLine(QStringLiteral("%1[%2] = %3;").arg(collectionName).arg(index).arg(valueExpr));
            return true;
        }

        QString valueExpr = literalForType(value, original, elementType);
        addCodeLine(QStringLiteral("%1[%2] = %3;").arg(collectionName).arg(index).arg(valueExpr));
        return true;
    }

    int ordinalToIndex(const QString &ordinal) const {
        static const QMap<QString, int> ordinals = {
            {QStringLiteral("primer"), 0},
            {QStringLiteral("segundo"), 1},
            {QStringLiteral("tercer"), 2},
            {QStringLiteral("cuarto"), 3},
            {QStringLiteral("quinto"), 4},
            {QStringLiteral("sexto"), 5},
            {QStringLiteral("septimo"), 6},
            {QStringLiteral("octavo"), 7},
            {QStringLiteral("noveno"), 8},
            {QStringLiteral("decimo"), 9}
        };
        if (ordinal == QStringLiteral("ultimo")) {
            return -1;
        }
        return ordinals.value(ordinal, 0);
    }

    bool handleAddToCollection(const QString &original, const QString &normalized) {
        QRegularExpression re(QStringLiteral("^(agregar|agrega|anadir|anade) (.+) (?:a|al|a la|a el) (lista|vector|arreglo)$"));
        QRegularExpressionMatch match = re.match(normalized);
        if (!match.hasMatch()) {
            return false;
        }

        QString valueText = match.captured(2).trimmed();
        QString alias = match.captured(3);

        QString collectionName = collectionNameForAlias(alias);
        if (collectionName.isEmpty()) {
            collectionName = lastCollection();
        }
        if (collectionName.isEmpty()) {
            notifyIssue(QStringLiteral("No se encontró ninguna colección disponible para esta instrucción."));
            return false;
        }

        const auto info = m_collections.value(collectionName);
        if (info.isCArray) {
            notifyIssue(QStringLiteral("No se pueden agregar elementos adicionales al arreglo %1."));
            return true;
        }

        QString elementType = info.elementType;
        if (elementType.isEmpty()) {
            notifyIssue(QStringLiteral("No se pudo determinar el tipo de datos de la colección."));
            return false;
        }
        QString valueExpr = literalForType(valueText, original, elementType);

        ensureInclude("vector");
        addCodeLine(QStringLiteral("%1.push_back(%2);").arg(collectionName, valueExpr));
        auto itAdd = m_collections.find(collectionName);
        if (itAdd != m_collections.end()) {
            if (itAdd->size >= 0) {
                itAdd->size += 1;
            }
        }
        return true;
    }

    bool handleRemoveFromCollection(const QString &original, const QString &normalized) {
        Q_UNUSED(original);
        QRegularExpression re(QStringLiteral("^(eliminar|quitar) el (primer|segundo|tercer|cuarto|quinto|sexto|septimo|octavo|noveno|decimo|ultimo) elemento de (?:la|el|del) (lista|vector|arreglo)$"));
        QRegularExpressionMatch match = re.match(normalized);
        if (!match.hasMatch()) {
            return false;
        }

        QString ordinal = match.captured(2);
        QString alias = match.captured(3);
        QString collectionName = collectionNameForAlias(alias);
        if (collectionName.isEmpty()) {
            collectionName = lastCollection();
        }
        if (collectionName.isEmpty()) {
            notifyIssue(QStringLiteral("No se encontró ninguna colección disponible para esta instrucción."));
            return false;
        }

        const auto info = m_collections.value(collectionName);
        if (info.isCArray) {
            notifyIssue(QStringLiteral("No se puede eliminar elementos en un arreglo de tamaño fijo."));
            return true;
        }

        ensureInclude("vector");

        int index = ordinalToIndex(ordinal);
        if (index >= 0) {
            int knownSize = collectionSize(collectionName);
            if (knownSize > 0 && index >= knownSize) {
                notifyIssue(QStringLiteral("El índice %1 está fuera de rango para la colección actual.").arg(index + 1));
            }
        }
        if (index < 0) {
            addCodeLine(QStringLiteral("if (!%1.empty()) { %1.pop_back(); }").arg(collectionName));
        } else {
            addCodeLine(QStringLiteral("if (%1.size() > %2) { %1.erase(%1.begin() + %2); }").arg(collectionName).arg(index));
        }
        auto itRemove = m_collections.find(collectionName);
        if (itRemove != m_collections.end()) {
            if (itRemove->size > 0) {
                itRemove->size -= 1;
            }
        }
        return true;
    }

    bool handleSortCollection(const QString &original, const QString &normalized) {
        Q_UNUSED(original);
        QRegularExpression re(QStringLiteral("^ordenar (?:la|el) (lista|vector|arreglo)(?: de forma (ascendente|descendente))?$"));
        QRegularExpressionMatch match = re.match(normalized);
        if (!match.hasMatch()) {
            return false;
        }

        QString alias = match.captured(1);
        QString order = match.captured(2);

        QString collectionName = collectionNameForAlias(alias);
        if (collectionName.isEmpty()) {
            collectionName = lastCollection();
        }
        if (collectionName.isEmpty()) {
            notifyIssue(QStringLiteral("No se encontró ninguna colección disponible para esta instrucción."));
            return false;
        }

        const auto info = m_collections.value(collectionName);
        QString elementType = info.elementType;
        if (elementType.isEmpty()) {
            notifyIssue(QStringLiteral("No se pudo determinar el tipo de datos de la colección."));
            return false;
        }
        ensureInclude("algorithm");
        if (info.isCArray) {
            if (info.size <= 0) {
                notifyIssue(QStringLiteral("No se conoce el tamaño del arreglo para ordenarlo."));
                return true;
            }
            if (order == QStringLiteral("descendente")) {
                addCodeLine(QStringLiteral("std::sort(%1, %1 + %2, [](const %3 &a, const %3 &b){ return a > b; });")
                                .arg(collectionName)
                                .arg(info.size)
                                .arg(elementType));
            } else {
                addCodeLine(QStringLiteral("std::sort(%1, %1 + %2);").arg(collectionName).arg(info.size));
            }
        } else {
            ensureInclude("vector");
            if (order == QStringLiteral("descendente")) {
                addCodeLine(QStringLiteral("std::sort(%1.begin(), %1.end(), [](const %2 &a, const %2 &b){ return a > b; });")
                                .arg(collectionName)
                                .arg(elementType));
            } else {
                addCodeLine(QStringLiteral("std::sort(%1.begin(), %1.end());").arg(collectionName));
            }
        }
        return true;
    }

    bool handleIterateCollection(const QString &original, const QString &normalized) {
        Q_UNUSED(original);
        if (!normalized.startsWith(QStringLiteral("recorrer la")) &&
            !normalized.startsWith(QStringLiteral("recorrer el"))) {
            return false;
        }

        // Simple "Recorrer la lista" pattern for general iteration with index
        QRegularExpression simpleRe("^recorrer (?:la|el) (lista|vector|arreglo)$");
        QRegularExpressionMatch simpleMatch = simpleRe.match(normalized);
        if (simpleMatch.hasMatch()) {
            QString alias = simpleMatch.captured(1);
            QString collectionName = collectionNameForAlias(alias);
            if (collectionName.isEmpty()) {
                collectionName = lastCollection();
            }
            if (collectionName.isEmpty()) {
                notifyIssue(QStringLiteral("No se encontró la colección a recorrer."));
                return true;
            }

            CollectionInfo info = m_collections.value(collectionName);
            QString indexName = QStringLiteral("i");
            
            // Check if 'i' already exists as a variable, if so use a different name
            if (hasVariable(indexName)) {
                indexName = QStringLiteral("idx");
            }
            
            if (info.isCArray) {
                startBlock(QStringLiteral("for (int %1 = 0; %1 < %2; ++%1) {").arg(indexName).arg(info.size), 
                          BlockType::Loop, true, m_currentIndent);
            } else {
                ensureInclude("cstddef");
                startBlock(QStringLiteral("for (std::size_t %1 = 0; %1 < %2.size(); ++%1) {").arg(indexName, collectionName), 
                          BlockType::Loop, true, m_currentIndent);
            }
            
            return true;
        }

        return handleIterateCollectionSum(original, normalized);
    }

    bool handleIterateCollectionSum(const QString &original, const QString &normalized) {
        Q_UNUSED(original);
        if (!normalized.startsWith(QStringLiteral("recorrer la"))) {
            return false;
        }

    QRegularExpression re("^recorrer la (lista|vector|arreglo) y sumar cada elemento (?:al|en) ([a-zA-Z_][a-zA-Z0-9_]*)$");
        QRegularExpressionMatch match = re.match(normalized);
        if (!match.hasMatch()) {
            return false;
        }

        QString alias = match.captured(1);
        QString destination = sanitizedIdentifier(match.captured(2));

        QString collectionName = collectionNameForAlias(alias);
        if (collectionName.isEmpty()) {
            collectionName = lastCollection();
        }
        if (collectionName.isEmpty()) {
            notifyIssue(QStringLiteral("No se encontró ninguna colección disponible para esta instrucción."));
            return false;
        }

        QString elementType = elementTypeForCollection(collectionName);
        if (elementType.isEmpty()) {
            notifyIssue(QStringLiteral("No se pudo determinar el tipo de datos de la colección."));
            return false;
        }
        QString itemName = QStringLiteral("item%1").arg(m_tempCounter++);
        addCodeLine(QStringLiteral("for (const %1 &%2 : %3) {").arg(elementType, itemName, collectionName));
        ++m_indentLevel;
        ensureVariable(destination, QStringLiteral("double"), QStringLiteral("0.0"));
        addCodeLine(QStringLiteral("%1 += %2;").arg(destination, itemName));
        --m_indentLevel;
        addCodeLine(QStringLiteral("}"));
        return true;
    }

    bool handleIfCondition(const QString &original, const QString &normalized) {
        if (!normalized.startsWith(QStringLiteral("si "))) {
            return false;
        }

        QString rest = normalized.mid(3);
        QString condition;
        QString action;

        int mostrarIdx = rest.indexOf(QStringLiteral(" mostrar "));
        int imprimirIdx = rest.indexOf(QStringLiteral(" imprimir "));
        int idx = mostrarIdx >= 0 ? mostrarIdx : imprimirIdx;
        QString actionVerb = mostrarIdx >= 0 ? QStringLiteral("mostrar") : QStringLiteral("imprimir");

        if (idx >= 0) {
            condition = rest.left(idx).trimmed();
            action = rest.mid(idx + actionVerb.size() + 1).trimmed();
        } else {
            condition = rest.trimmed();
        }

        QString conditionExpr = translateCondition(condition);
        if (conditionExpr.isEmpty()) {
            notifyIssue(QStringLiteral("No se pudo interpretar la condición del 'si': %1").arg(condition));
            return false;
        }

        startBlock(QStringLiteral("if (%1) {").arg(conditionExpr), BlockType::If, true, m_currentIndent);

        if (!action.isEmpty()) {
            QString message = readQuotedText(original);
            if (message.isEmpty()) {
                message = action;
            }
            ensureInclude("iostream");
            addCodeLine(QStringLiteral("std::cout << %1 << std::endl;").arg(quoted(message)));
        }

        return true;
    }

    QString translateCondition(const QString &condition) {
        QString norm = condition.trimmed();
        QString normalized = normalizeLine(norm);
        normalized.replace(QStringLiteral(" es " ), QStringLiteral(" "));

        struct {
            QString keyword;
            QString op;
        } ops[] = {
            {QStringLiteral("mayor que"), QStringLiteral(">")},
            {QStringLiteral("menor que"), QStringLiteral("<")},
            {QStringLiteral("mayor o igual que"), QStringLiteral(">=")},
            {QStringLiteral("menor o igual que"), QStringLiteral("<=")},
            {QStringLiteral("igual a"), QStringLiteral("==")},
            {QStringLiteral("diferente de"), QStringLiteral("!=")}
        };

        for (const auto &entry : ops) {
            int idx = normalized.indexOf(entry.keyword);
            if (idx >= 0) {
                QString left = normalized.left(idx).trimmed();
                QString right = normalized.mid(idx + entry.keyword.size()).trimmed();
                
                QString leftExpr = translateExpressionPart(left);
                QString rightExpr = translateExpressionPart(right);
                
                if (!leftExpr.isEmpty() && !rightExpr.isEmpty()) {
                    return QStringLiteral("%1 %2 %3").arg(leftExpr, entry.op, rightExpr);
                }
            }
        }

        return QString();
    }

    QString translateExpressionPart(const QString &part) {
        QString trimmed = part.trimmed();
        
        // Check for array access pattern like "lista[i]"
        QRegularExpression arrayAccess(QStringLiteral("^([a-zA-Z_][a-zA-Z0-9_]*)\\[([a-zA-Z_][a-zA-Z0-9_]*)\\]$"));
        QRegularExpressionMatch arrayMatch = arrayAccess.match(trimmed);
        if (arrayMatch.hasMatch()) {
            QString arrayName = sanitizedIdentifier(arrayMatch.captured(1));
            QString indexName = sanitizedIdentifier(arrayMatch.captured(2));
            
            // Find collection by alias
            if (!hasCollection(arrayName)) {
                arrayName = collectionNameForAlias(arrayMatch.captured(1));
            }
            
            if (hasCollection(arrayName)) {
                // Use the actual variable name if it exists, otherwise use the provided index
                if (hasVariable(indexName)) {
                    return QStringLiteral("%1[%2]").arg(arrayName, indexName);
                } else if (hasVariable(QStringLiteral("idx"))) {
                    return QStringLiteral("%1[idx]").arg(arrayName);
                } else if (hasVariable(QStringLiteral("i"))) {
                    return QStringLiteral("%1[i]").arg(arrayName);
                } else {
                    return QStringLiteral("%1[%2]").arg(arrayName, indexName);
                }
            }
        }
        
        // Check if it's a number
        QRegularExpression numberRegex(QStringLiteral("^-?\\d+(?:[\\.,]\\d+)?$"));
        if (numberRegex.match(trimmed).hasMatch()) {
            return ensureNumberString(trimmed, trimmed.contains('.') || trimmed.contains(','));
        }
        
        // Check if it's a boolean value
        if (trimmed == QStringLiteral("verdadero") || trimmed == QStringLiteral("true")) {
            return QStringLiteral("true");
        }
        if (trimmed == QStringLiteral("falso") || trimmed == QStringLiteral("false")) {
            return QStringLiteral("false");
        }
        
        // Check if it's a variable
        QString identifier = sanitizedIdentifier(trimmed);
        if (hasVariable(identifier)) {
            return identifier;
        }
        
        return trimmed;
    }

    bool handleElse(const QString &original, const QString &normalized) {
        Q_UNUSED(normalized);
        if (m_blocks.isEmpty() || m_blocks.last().type != BlockType::If) {
            notifyIssue(QStringLiteral("Se encontró un 'sino' sin un 'si' previo."));
            return false;
        }

        if (m_blocks.last().hasElse) {
            notifyIssue(QStringLiteral("El bloque 'si' ya tenía un 'sino' asociado."));
            return false;
        }

        if (m_indentLevel > 1) {
            --m_indentLevel;
        }
        m_codeLines.append(indent() + QStringLiteral("} else {"));
        ++m_indentLevel;
        m_blocks.last().hasElse = true;
        m_blocks.last().autoClose = true;
        m_blocks.last().indent = m_currentIndent;

        QString message = readQuotedText(original);
        if (!message.isEmpty()) {
            ensureInclude("iostream");
            addCodeLine(QStringLiteral("std::cout << %1 << std::endl;").arg(quoted(message)));
        }
        return true;
    }

    bool handleShowMessage(const QString &original, const QString &normalized) {
        if (!normalized.startsWith(QStringLiteral("mostrar")) &&
            !normalized.startsWith(QStringLiteral("imprimir"))) {
            return false;
        }

        QString message = readQuotedText(original);
        QString appendedVar;

        int firstQuote = original.indexOf('"');
        int secondQuote = (firstQuote >= 0) ? original.indexOf('"', firstQuote + 1) : -1;
        if (secondQuote >= 0) {
            QString tail = original.mid(secondQuote + 1).trimmed();
            if (tail.startsWith(QStringLiteral("y "))) {
                appendedVar = sanitizedIdentifier(tail.mid(2).trimmed());
            }
        }

        if (message.isEmpty()) {
            QString rest = normalized;
            int idx = rest.indexOf(' ');
            if (idx >= 0) {
                message = rest.mid(idx + 1).trimmed();
            } else {
                message = rest;
            }
        }

        QStringList parts;
        parts << quoted(message);

        if (!appendedVar.isEmpty()) {
            if (!hasVariable(appendedVar)) {
                notifyIssue(QStringLiteral("La variable %1 no existe para mostrar su valor.").arg(appendedVar));
            } else {
                parts << appendedVar;
            }
        }

        ensureInclude("iostream");
        addCodeLine(QStringLiteral("std::cout << %1 << std::endl;").arg(parts.join(QStringLiteral(" << "))));
        return true;
    }

    bool requestInputForCollection(const QString &alias) {
        QString collection = alias.isEmpty() ? lastCollection() : collectionNameForAlias(alias);
        if (collection.isEmpty()) {
            collection = lastCollection();
        }
        if (collection.isEmpty()) {
            notifyIssue(QStringLiteral("No se encontró ninguna colección disponible para ingresar datos."));
            return true;
        }

        if (!hasCollection(collection)) {
            notifyIssue(QStringLiteral("No se encontró ninguna colección disponible para ingresar datos."));
            return true;
        }

        CollectionInfo info = m_collections.value(collection);
        ensureInclude("iostream");
        ensureInclude("cstddef");

        QString indexName = QStringLiteral("i%1").arg(m_tempCounter++);
        const QString promptLine = QStringLiteral(R"(std::cout << "Ingrese el valor " << (%1 + 1) << ": ";)");

        if (info.isCArray) {
            if (info.size <= 0) {
                notifyIssue(QStringLiteral("No se conoce el tamaño del arreglo para solicitar entradas de usuario."));
                return true;
            }
            addCodeLine(QStringLiteral("for (std::size_t %1 = 0; %1 < %2; ++%1) {").arg(indexName).arg(info.size));
            ++m_indentLevel;
            addCodeLine(promptLine.arg(indexName));
            addCodeLine(QStringLiteral("std::cin >> %1[%2];").arg(collection, indexName));
            --m_indentLevel;
            addCodeLine(QStringLiteral("}"));
            return true;
        }

        ensureInclude("vector");
        addCodeLine(QStringLiteral("for (std::size_t %1 = 0; %1 < %2.size(); ++%1) {").arg(indexName, collection));
        ++m_indentLevel;
        addCodeLine(promptLine.arg(indexName));
        addCodeLine(QStringLiteral("std::cin >> %1[%2];").arg(collection, indexName));
        --m_indentLevel;
        addCodeLine(QStringLiteral("}"));
        return true;
    }

    bool handleUserInput(const QString &original, const QString &normalized) {
        Q_UNUSED(original);

        bool matched = false;
        QString collectionAlias;

        if (normalized.startsWith(QStringLiteral("pedir al usuario"))) {
            matched = true;
        } else if (normalized.startsWith(QStringLiteral("ingresar valor de cada " ))) {
            QRegularExpression re(QStringLiteral("^ingresar valor de cada (.+) en (?:la|el) (lista|vector|arreglo)$"));
            QRegularExpressionMatch match = re.match(normalized);
            if (match.hasMatch()) {
                collectionAlias = match.captured(2);
                matched = true;
            }
        }

        if (!matched) {
            return false;
        }

        return requestInputForCollection(collectionAlias);
    }

    bool handlePrintCollection(const QString &original, const QString &normalized) {
        Q_UNUSED(original);
        if (!normalized.startsWith(QStringLiteral("imprimir todos los elementos del")) &&
            !normalized.startsWith(QStringLiteral("mostrar todos los elementos del"))) {
            return false;
        }

        QString collection = lastCollection();
        if (collection.isEmpty()) {
            return false;
        }

        CollectionInfo info = m_collections.value(collection);
        ensureInclude("iostream");
        QString indexName = QStringLiteral("valor%1").arg(m_tempCounter++);
        if (info.isCArray) {
            if (info.size <= 0) {
                notifyIssue(QStringLiteral("No se conoce el tamaño del arreglo para imprimir sus elementos."));
                return true;
            }
            addCodeLine(QStringLiteral("for (int %1 = 0; %1 < %2; ++%1) {").arg(indexName).arg(info.size));
            ++m_indentLevel;
            addCodeLine(QStringLiteral("std::cout << %1[%2] << std::endl;").arg(collection).arg(indexName));
            --m_indentLevel;
            addCodeLine(QStringLiteral("}"));
        } else {
            ensureInclude("vector");
            addCodeLine(QStringLiteral("for (const %1 &%2 : %3) {").arg(info.elementType, indexName, collection));
            ++m_indentLevel;
            addCodeLine(QStringLiteral("std::cout << %1 << std::endl;").arg(indexName));
            --m_indentLevel;
            addCodeLine(QStringLiteral("}"));
        }
        return true;
    }

    bool handleReadDataFile(const QString &original, const QString &normalized) {
        Q_UNUSED(original);
        if (!normalized.startsWith(QStringLiteral("leer los datos"))) {
            return false;
        }

        QRegularExpression re("archivo llamado ([^\\s]+)");
        QRegularExpressionMatch match = re.match(normalized);
        QString fileName = match.hasMatch() ? match.captured(1).trimmed() : m_dataFileName;
        if (fileName.isEmpty()) {
            fileName = m_dataFileName;
        }

        if (!m_input.dataFileContents.trimmed().isEmpty()) {
            ensureDataFileWritten(fileName, m_input.dataFileContents);
        }

        QString paises = collectionNameForAlias(QStringLiteral("paises"));
        QString capitales = collectionNameForAlias(QStringLiteral("capitales"));
        if (paises.isEmpty() || capitales.isEmpty()) {
            // fallback to last two vectors
            QStringList keys = m_collections.keys();
            if (keys.size() >= 2) {
                paises = keys.at(keys.size() - 2);
                capitales = keys.at(keys.size() - 1);
            }
        }

        ensureInclude("fstream");
        ensureInclude("sstream");
        ensureInclude("string");
        ensureInclude("vector");
        ensureInclude("iostream");
        addCodeLine(QStringLiteral("std::ifstream archivo(%1);").arg(quoted(fileName)));
        addCodeLine(QStringLiteral("if (!archivo) {"));
        ++m_indentLevel;
        addCodeLine(QStringLiteral("std::cerr << \"No se pudo abrir %1\" << std::endl;").arg(escapeForStringLiteral(fileName)));
        addCodeLine(QStringLiteral("return 1;"));
        --m_indentLevel;
        addCodeLine(QStringLiteral("}"));
        addCodeLine(QStringLiteral("std::string linea;"));
        addCodeLine(QStringLiteral("while (std::getline(archivo, linea)) {"));
        ++m_indentLevel;
        addCodeLine(QStringLiteral("std::stringstream ss(linea);"));
        addCodeLine(QStringLiteral("std::string campo1;"));
        addCodeLine(QStringLiteral("std::string campo2;"));
        addCodeLine(QStringLiteral("if (std::getline(ss, campo1, ',') && std::getline(ss, campo2)) {"));
        ++m_indentLevel;
        if (!paises.isEmpty()) {
            addCodeLine(QStringLiteral("%1.push_back(campo1);").arg(paises));
        }
        if (!capitales.isEmpty()) {
            addCodeLine(QStringLiteral("%1.push_back(campo2);").arg(capitales));
        }
        --m_indentLevel;
        addCodeLine(QStringLiteral("}"));
        --m_indentLevel;
        addCodeLine(QStringLiteral("}"));
        m_requiresDataFile = true;
        return true;
    }

    void ensureDataFileWritten(const QString &fileName, const QString &contents) {
        if (m_dataWritten) {
            return;
        }
        ensureInclude("fstream");
        addStartupLine(QStringLiteral("{"), 1);
        addStartupLine(QStringLiteral("std::ofstream datos(%1);").arg(quoted(fileName)), 2);
        QStringList lines = contents.split('\n');
        for (const QString &line : lines) {
            QString trimmed = line;
            if (trimmed.endsWith('\r')) {
                trimmed.chop(1);
            }
            QString literal = quoted(trimmed);
            addStartupLine(QStringLiteral("datos << %1 << '\\n';").arg(literal), 2);
        }
        addStartupLine(QStringLiteral("datos.close();"), 2);
        addStartupLine(QStringLiteral("}"), 1);
        m_dataWritten = true;
    }

    bool handlePrintPairs(const QString &original, const QString &normalized) {
        Q_UNUSED(original);
        if (!normalized.startsWith(QStringLiteral("imprimir los paises"))) {
            return false;
        }

        QString paises = collectionNameForAlias(QStringLiteral("paises"));
        QString capitales = collectionNameForAlias(QStringLiteral("capitales"));
        if (paises.isEmpty() || capitales.isEmpty()) {
            return false;
        }

        ensureInclude("iostream");
        ensureInclude("vector");
        ensureInclude("string");
        addCodeLine(QStringLiteral("for (std::size_t i = 0; i < %1.size() && i < %2.size(); ++i) {").arg(paises, capitales));
        ++m_indentLevel;
        addCodeLine(QStringLiteral("std::cout << %1[i] << \" - \" << %2[i] << std::endl;").arg(paises, capitales));
        --m_indentLevel;
        addCodeLine(QStringLiteral("}"));
        return true;
    }

private:
    const Parser::Input m_input;
    QStringList m_codeLines;
    QVector<QPair<int, QString>> m_startupLines;
    QSet<QString> m_includes;
    QVector<BlockState> m_blocks;
    QMap<QString, VariableInfo> m_variables;
    QMap<QString, CollectionInfo> m_collections;
    QString m_lastCollection;
    QStringList m_issues;
    bool m_success = true;
    int m_indentLevel = 1;
    int m_currentIndent = 0;
    int m_tempCounter = 1;
    QString m_dataFileName;
    bool m_dataWritten = false;
    bool m_requiresDataFile = false;
};

} // namespace

Parser::Output Parser::convert(const Parser::Input &input) {
    InstructionParser parser(input);
    return parser.run();
}
