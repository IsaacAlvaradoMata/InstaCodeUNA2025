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

namespace
{

    QString removeDiacritics(const QString &text)
    {
        QString decomposed = text.normalized(QString::NormalizationForm_D);
        QString result;
        result.reserve(decomposed.size());
        for (const QChar &ch : decomposed)
        {
            if (ch.category() != QChar::Mark_NonSpacing &&
                ch.category() != QChar::Mark_SpacingCombining &&
                ch.category() != QChar::Mark_Enclosing)
            {
                result.append(ch);
            }
        }
        return result;
    }

    QString normalizeLine(const QString &line)
    {
        QString simplified = removeDiacritics(line).toLower();
        simplified.replace('\r', ' ');
        simplified.replace('\n', ' ');
        simplified.replace(QRegularExpression("[\\s]+"), " ");
        return simplified.trimmed();
    }

    QString sanitizedIdentifier(const QString &source)
    {
        QString ascii = removeDiacritics(source).toLower();
        QString result;
        bool lastWasUnderscore = false;
        for (const QChar &ch : ascii)
        {
            if (ch.isLetterOrNumber())
            {
                result.append(ch);
                lastWasUnderscore = false;
            }
            else if (!lastWasUnderscore)
            {
                if (!result.isEmpty())
                {
                    result.append('_');
                }
                lastWasUnderscore = true;
            }
        }
        while (!result.isEmpty() && result.endsWith('_'))
        {
            result.chop(1);
        }
        if (result.isEmpty())
        {
            result = QStringLiteral("valor");
        }
        if (result.front().isDigit())
        {
            result.prepend('v');
        }
        return result;
    }

    QString escapeForStringLiteral(const QString &text)
    {
        QString escaped;
        escaped.reserve(text.size() + 8);
        for (const QChar &ch : text)
        {
            if (ch == '"' || ch == '\\')
            {
                escaped.append('\\');
                escaped.append(ch);
            }
            else if (ch == '\n')
            {
                escaped.append("\\n");
            }
            else if (ch == '\r')
            {
                continue;
            }
            else
            {
                escaped.append(ch);
            }
        }
        return escaped;
    }

    QString quoted(const QString &text)
    {
        return QStringLiteral("\"") + escapeForStringLiteral(text) + QStringLiteral("\"");
    }

    QString ensureNumberString(const QString &value, bool floating)
    {
        QString cleaned = value.trimmed();
        if (cleaned.isEmpty())
        {
            return floating ? QStringLiteral("0.0") : QStringLiteral("0");
        }
        cleaned.replace(',', '.');
        if (floating && !cleaned.contains('.'))
        {
            cleaned.append(".0");
        }
        return cleaned;
    }

    QString readQuotedText(const QString &line)
    {
        int first = line.indexOf('"');
        if (first < 0)
        {
            return QString();
        }
        int second = line.indexOf('"', first + 1);
        if (second < 0)
        {
            return QString();
        }
        return line.mid(first + 1, second - first - 1);
    }
    QStringList splitLines(const QString &text)
    {
        return text.split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts);
    }

    enum class BlockType
    {
        Generic,
        If,
        Loop
    };

    struct BlockState
    {
        BlockType type = BlockType::Generic;
        bool autoClose = false;
        bool hasElse = false;
        bool hasElseIf = false;
        int indent = 0;
    };

    struct VariableInfo
    {
        QString type;
        bool fromInstruction = false;
    };

    struct CollectionInfo
    {
        QString type;
        QString elementType;
        QString alias;
        int size = 0;
        bool fixedSize = false;
        bool isCArray = false;
    };

    struct FunctionInfo
    {
        QString name;
        QString returnType;
        QStringList parameterTypes;
        QStringList parameterNames;
        QStringList body;
    };

    struct StructInfo
    {
        QString name;
        QStringList fieldNames;
        QStringList fieldTypes;
    };

    class InstructionParser
    {
    public:
        explicit InstructionParser(const Parser::Input &input)
            : m_input(input)
        {
            ensureInclude("iostream");
            m_dataFileName = input.dataFileName.trimmed();
            if (m_dataFileName.isEmpty())
            {
                m_dataFileName = QStringLiteral("datos.txt");
            }
        }

        Parser::Output run()
        {
            const QStringList lines = splitLines(m_input.instructions);

            bool needsDataFile = false;
            for (const QString &rawLine : lines)
            {
                QString normalized = normalizeLine(rawLine);
                if ((normalized.contains(QStringLiteral("leer")) && (normalized.contains(QStringLiteral("datos")) || normalized.contains(QStringLiteral("archivo")))) ||
                    (normalized.contains(QStringLiteral("cargar")) && (normalized.contains(QStringLiteral("datos")) || normalized.contains(QStringLiteral("archivo")))) ||
                    (normalized.contains(QStringLiteral("importar")) && (normalized.contains(QStringLiteral("datos")) || normalized.contains(QStringLiteral("archivo")))) ||
                    ((normalized.contains(QStringLiteral("imprimir")) || normalized.contains(QStringLiteral("mostrar"))) &&
                     normalized.contains(QStringLiteral("paises")) && normalized.contains(QStringLiteral("capitales"))))
                {
                    needsDataFile = true;
                    break;
                }
            }

            if (needsDataFile && m_input.dataFileContents.trimmed().isEmpty())
            {
                Parser::Output result;
                result.code = QString();
                result.issues.append(QStringLiteral("Error: Las instrucciones requieren un archivo de datos, pero no se ha cargado ninguno. Use el botón 'Cargar Datos' para cargar un archivo .txt antes de convertir."));
                result.success = false;
                return result;
            }

            for (const QString &rawLine : lines)
            {
                QString trimmed = rawLine.trimmed();
                if (trimmed.isEmpty())
                {
                    continue;
                }

                int leadingSpaces = rawLine.indexOf(trimmed);
                if (leadingSpaces < 0)
                    leadingSpaces = 0;
                QString normalized = normalizeLine(trimmed);

                bool isSino = normalized.startsWith(QStringLiteral("sino"));
                if (!isSino)
                {
                    closeAutoBlocks(leadingSpaces);
                }

                m_currentIndent = leadingSpaces;

                if (!processLine(trimmed, normalized))
                {
                    m_success = false;
                    m_issues.append(QStringLiteral("Instrucción no reconocida: %1").arg(trimmed));
                }
            }

            closeAutoBlocks(0);
            closeAllBlocks();

            QStringList output;
            QStringList includeList = m_includes.values();
            std::sort(includeList.begin(), includeList.end());
            for (const QString &inc : includeList)
            {
                output << QStringLiteral("#include <%1>").arg(inc);
            }
            output << QString();

            for (auto it = m_functions.constBegin(); it != m_functions.constEnd(); ++it)
            {
                const FunctionInfo &func = it.value();
                QString signature = QStringLiteral("%1 %2(").arg(func.returnType, func.name);
                for (int i = 0; i < func.parameterTypes.size(); ++i)
                {
                    if (i > 0)
                        signature += QStringLiteral(", ");
                    signature += QStringLiteral("%1 %2").arg(func.parameterTypes[i], func.parameterNames[i]);
                }
                signature += QStringLiteral(") {");
                output << signature;
                for (const QString &line : func.body)
                {
                    output << line;
                }
                output << QStringLiteral("}");
                output << QString();
            }

            for (auto it = m_structs.constBegin(); it != m_structs.constEnd(); ++it)
            {
                const StructInfo &structInfo = it.value();
                output << QStringLiteral("struct %1 {").arg(structInfo.name);
                for (int i = 0; i < structInfo.fieldNames.size() && i < structInfo.fieldTypes.size(); ++i)
                {
                    output << QStringLiteral("    %1 %2;").arg(structInfo.fieldTypes[i], structInfo.fieldNames[i]);
                }
                output << "};";
                output << QString();
            }

            output << QString();
            output << QStringLiteral("int main() {");

            if (!m_startupLines.isEmpty())
            {
                for (const auto &line : m_startupLines)
                {
                    output << QStringLiteral("%1%2").arg(QStringLiteral("    ").repeated(line.first), line.second);
                }
                if (!m_codeLines.isEmpty())
                {
                    output << QString();
                }
            }

            for (const QString &line : m_codeLines)
            {
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
        bool processLine(const QString &original, const QString &normalized)
        {
            if (normalized.isEmpty())
            {
                return true;
            }

            QString core = normalized;
            if (core.endsWith('.'))
            {
                core.chop(1);
            }

            if (core == QStringLiteral("comenzar programa") ||
                core == QStringLiteral("terminar programa"))
            {
                return true;
            }

            if (core.startsWith(QStringLiteral("sino")))
            {
                return handleElse(original, core);
            }

            if (handleCreateVariable(original, core))
                return true;
            if (handleDefineFunction(original, core))
                return true;
            if (handleReturnStatement(original, core))
                return true;
            if (handleFunctionCall(original, core))
                return true;
            if (handleCreateStruct(original, core))
                return true;
            if (handleCreateStructCollection(original, core))
                return true;
            if (handleInputStructData(original, core))
                return true;
            if (handleIterateStructCollection(original, core))
                return true;
            if (handleCompoundArithmeticInstruction(original, core))
                return true;
            if (handleAssignCollectionElement(original, core))
                return true;
            if (handleAssignValue(original, core))
                return true;
            if (handleVariableOperation(original, core))
                return true;
            if (handleCalculateExpression(original, core))
                return true;
            if (handleCalculateAverage(original, core))
                return true;
            if (handleUserInput(original, core))
                return true;
            if (handleRequestNumberInput(original, core))
                return true;
            if (handleInputValue(original, core))
                return true;
            if (handleArithmeticBinary(original, core))
                return true;
            if (handleArithmeticAggregate(original, core))
                return true;
            if (handleRepeatMessage(original, core))
                return true;
            if (handleWhileIncrease(original, core))
                return true;
            if (handleCreateCollection(original, core))
                return true;
            if (handleIterateCollectionSum(original, core))
                return true;
            if (handleAddToCollection(original, core))
                return true;
            if (handleRemoveFromCollection(original, core))
                return true;
            if (handleSortCollection(original, core))
                return true;
            if (handleIterateCollection(original, core))
                return true;
            if (handleIfCondition(original, core))
                return true;
            if (handlePrintPairs(original, core))
                return true;
            if (handlePrintCollection(original, core))
                return true;
            if (handleShowMessage(original, core))
                return true;
            if (handleReadDataFile(original, core))
                return true;
            if (core.startsWith(QStringLiteral("guardar los numeros en")))
            {
                return true;
            }

            return false;
        }

        void ensureInclude(const QString &include)
        {
            m_includes.insert(include);
        }

        QString indent() const
        {
            return QStringLiteral("    ").repeated(m_indentLevel);
        }

        void addCodeLine(const QString &line)
        {
            m_codeLines.append(indent() + line);
        }

        void notifyIssue(const QString &message)
        {
            m_issues.append(message);
        }

        void addStartupLine(const QString &line, int indentLevel = 1)
        {
            m_startupLines.append({indentLevel, line});
        }

        void startBlock(const QString &header, BlockType type, bool autoClose = false, int indentLevel = 0)
        {
            m_codeLines.append(indent() + header);
            m_blocks.push_back({type, autoClose, false, false, indentLevel});

            ++m_indentLevel;
        }

        void endBlock()
        {
            if (m_indentLevel > 1)
            {
                --m_indentLevel;
            }
            m_codeLines.append(indent() + QStringLiteral("}"));
            if (!m_blocks.isEmpty())
            {
                m_blocks.removeLast();
            }
        }

        void closeAutoBlocks(int currentIndent)
        {
            while (!m_blocks.isEmpty())
            {
                auto &block = m_blocks.last();
                if (!block.autoClose)
                {
                    break;
                }
                if (block.hasElse && currentIndent > block.indent)
                {
                    break;
                }
                if (currentIndent > block.indent)
                {
                    break;
                }
                endBlock();
            }

            if (currentIndent == 0 && m_insideFunction && !m_currentFunctionName.isEmpty())
            {

                if (!m_functions[m_currentFunctionName].body.isEmpty() &&
                    m_functions[m_currentFunctionName].body.last().contains("while"))
                {
                    m_functions[m_currentFunctionName].body.append(QStringLiteral("    }"));
                }
            }
        }

        void closeAllBlocks()
        {
            while (!m_blocks.isEmpty())
            {
                endBlock();
            }
        }

        void registerVariable(const QString &name, const QString &type, bool byInstruction = true)
        {
            m_variables.insert(name, {type, byInstruction});
        }

        bool hasVariable(const QString &name) const
        {
            return m_variables.contains(name);
        }

        QString variableType(const QString &name) const
        {
            return m_variables.value(name).type;
        }

        void ensureVariable(const QString &name, const QString &type, const QString &initializer)
        {
            if (hasVariable(name))
            {
                return;
            }

            if (m_insideFunction && !m_currentFunctionName.isEmpty())
            {
                QString line = QStringLiteral("    %1 %2 = %3;").arg(type, name, initializer);
                m_functions[m_currentFunctionName].body.append(line);
            }
            else
            {
                addCodeLine(QStringLiteral("%1 %2 = %3;").arg(type, name, initializer));
            }
            registerVariable(name, type, false);
        }

        QString getUniqueVariableName(const QString &baseName)
        {
            QString candidate = baseName;
            int suffix = 1;
            while (hasVariable(candidate) || m_collections.contains(candidate))
            {
                candidate = baseName + QString::number(suffix++);
            }
            return candidate;
        }

        void registerCollection(const QString &name, const CollectionInfo &info)
        {
            m_collections.insert(name, info);
            m_collectionOrder.append(name);
            m_lastCollection = name;
        }

        bool hasCollection(const QString &name) const
        {
            return m_collections.contains(name);
        }

        bool hasFunction(const QString &name) const
        {
            return m_functions.contains(name);
        }

        QString collectionNameForAlias(const QString &alias) const
        {
            QString key = sanitizedIdentifier(alias);
            for (auto it = m_collections.constBegin(); it != m_collections.constEnd(); ++it)
            {
                if (it.value().alias == key)
                {
                    return it.key();
                }
            }

            if (alias == QStringLiteral("paises") || alias == QStringLiteral("países"))
            {
                for (auto it = m_collections.constBegin(); it != m_collections.constEnd(); ++it)
                {
                    if (it.value().alias == QStringLiteral("paises") || it.key().contains(QStringLiteral("paises")))
                    {
                        return it.key();
                    }
                }
            }
            if (alias == QStringLiteral("capitales"))
            {
                for (auto it = m_collections.constBegin(); it != m_collections.constEnd(); ++it)
                {
                    if (it.value().alias == QStringLiteral("capitales") || it.key().contains(QStringLiteral("capitales")))
                    {
                        return it.key();
                    }
                }
            }

            return QString();
        }

        QString lastCollection() const
        {
            if (!m_lastCollection.isEmpty())
            {
                return m_lastCollection;
            }
            if (!m_collections.isEmpty())
            {
                return m_collections.constBegin().key();
            }
            return QString();
        }

        QString elementTypeForCollection(const QString &name) const
        {
            return m_collections.value(name).elementType;
        }

        int collectionSize(const QString &name) const
        {
            return m_collections.value(name).size;
        }

        bool isCollectionFixedSize(const QString &name) const
        {
            return m_collections.value(name).fixedSize;
        }

        bool isCollectionCArray(const QString &name) const
        {
            return m_collections.value(name).isCArray;
        }

        bool handleCreateVariable(const QString &original, const QString &normalized)
        {
            QString keyword;
            if (normalized.startsWith(QStringLiteral("crear variable")))
            {
                keyword = QStringLiteral("crear variable");
            }
            else if (normalized.startsWith(QStringLiteral("definir variable")))
            {
                keyword = QStringLiteral("definir variable");
            }
            else
            {
                return false;
            }

            QString rest = normalized.mid(keyword.size()).trimmed();
            QString typeToken;
            QString nameToken;
            QString valueToken;

            const struct
            {
                QString key;
                QString type;
                bool floating;
            } typeMap[] = {
                {QStringLiteral("numero decimal"), QStringLiteral("double"), true},
                {QStringLiteral("numero entero"), QStringLiteral("int"), false},
                {QStringLiteral("texto"), QStringLiteral("std::string"), false},
                {QStringLiteral("cadena"), QStringLiteral("std::string"), false},
                {QStringLiteral("booleano"), QStringLiteral("bool"), false}};

            QString chosenType;
            bool isFloating = false;
            for (const auto &entry : typeMap)
            {
                if (rest.startsWith(entry.key))
                {
                    chosenType = entry.type;
                    isFloating = entry.floating;
                    typeToken = entry.key;
                    break;
                }
            }

            if (chosenType.isEmpty())
            {
                return false;
            }

            QString afterType = rest.mid(typeToken.size()).trimmed();
            if (afterType.isEmpty())
            {
                return false;
            }

            int valueIdx = afterType.indexOf(QStringLiteral("con valor inicial"));
            if (valueIdx >= 0)
            {
                nameToken = afterType.left(valueIdx).trimmed();
                valueToken = afterType.mid(valueIdx + QStringLiteral("con valor inicial").size()).trimmed();
            }
            else
            {
                nameToken = afterType.trimmed();
            }

            if (nameToken.isEmpty())
            {
                nameToken = QStringLiteral("variable");
            }

            QString identifier = sanitizedIdentifier(nameToken);

            QString initializer;
            if (!valueToken.isEmpty())
            {
                if (chosenType == QStringLiteral("std::string"))
                {
                    QString quotedText = readQuotedText(original);
                    if (quotedText.isEmpty())
                    {
                        initializer = quoted(valueToken);
                    }
                    else
                    {
                        initializer = quoted(quotedText);
                    }
                    ensureInclude("string");
                }
                else if (chosenType == QStringLiteral("bool"))
                {
                    if (valueToken.contains(QStringLiteral("verdadero")))
                    {
                        initializer = QStringLiteral("true");
                    }
                    else if (valueToken.contains(QStringLiteral("falso")))
                    {
                        initializer = QStringLiteral("false");
                    }
                    else
                    {
                        initializer = QStringLiteral("false");
                    }
                }
                else
                {
                    initializer = ensureNumberString(valueToken, isFloating);
                }
            }
            else
            {
                if (chosenType == QStringLiteral("std::string"))
                {
                    initializer = QStringLiteral("\"\"");
                    ensureInclude("string");
                }
                else if (chosenType == QStringLiteral("bool"))
                {
                    initializer = QStringLiteral("false");
                }
                else
                {
                    initializer = isFloating ? QStringLiteral("0.0") : QStringLiteral("0");
                }
            }

            if (m_insideFunction && !m_currentFunctionName.isEmpty())
            {

                QString line = QStringLiteral("    %1 %2 = %3;").arg(chosenType, identifier, initializer);
                m_functions[m_currentFunctionName].body.append(line);
            }
            else
            {

                addCodeLine(QStringLiteral("%1 %2 = %3;").arg(chosenType, identifier, initializer));
            }
            registerVariable(identifier, chosenType);

            return true;
        }

        bool handleAssignValue(const QString &original, const QString &normalized)
        {
            if (!normalized.startsWith(QStringLiteral("asignar valor")) &&
                !normalized.startsWith(QStringLiteral("asignar")))
            {
                return false;
            }

            QString rest;
            if (normalized.startsWith(QStringLiteral("asignar valor")))
            {
                rest = normalized.mid(QStringLiteral("asignar valor").size()).trimmed();
            }
            else
            {
                rest = normalized.mid(QStringLiteral("asignar").size()).trimmed();
            }

            int toIdx = rest.lastIndexOf(QStringLiteral(" a "));
            if (toIdx < 0)
            {
                toIdx = rest.lastIndexOf(QStringLiteral(" al "));
            }
            if (toIdx < 0)
            {
                return false;
            }

            QString valuePart = rest.left(toIdx).trimmed();
            QString namePart = rest.mid(toIdx).trimmed();

            if (namePart.startsWith(QStringLiteral("a ")))
            {
                namePart = namePart.mid(2).trimmed();
            }
            if (namePart.startsWith(QStringLiteral("al ")))
            {
                namePart = namePart.mid(3).trimmed();
            }

            if (namePart.startsWith(QStringLiteral("valor de ")))
            {
                namePart = namePart.mid(QStringLiteral("valor de ").size()).trimmed();
            }

            QString identifier = sanitizedIdentifier(namePart);
            if (!hasVariable(identifier))
            {

                QString varType = QStringLiteral("int");
                QString initialValue = QStringLiteral("0");

                QString quotedText = readQuotedText(original);
                if (!quotedText.isEmpty())
                {
                    varType = QStringLiteral("std::string");
                    initialValue = QStringLiteral("\"\"");
                    ensureInclude("string");
                }
                else if (valuePart == QStringLiteral("verdadero") ||
                         valuePart == QStringLiteral("falso") ||
                         valuePart == QStringLiteral("true") ||
                         valuePart == QStringLiteral("false"))
                {
                    varType = QStringLiteral("bool");
                    initialValue = QStringLiteral("false");
                }
                else if (isDecimalNumber(valuePart))
                {
                    varType = QStringLiteral("double");
                    initialValue = QStringLiteral("0.0");
                }

                ensureVariable(identifier, varType, initialValue);
            }

            QString valueExpr;
            QString quotedText = readQuotedText(original);
            if (!quotedText.isEmpty())
            {
                valueExpr = quoted(quotedText);
            }
            else
            {
                valueExpr = translateExpression(valuePart, original);
            }

            addCodeLine(QStringLiteral("%1 = %2;").arg(identifier, valueExpr));
            return true;
        }

        bool handleDefineFunction(const QString &original, const QString &normalized)
        {
            Q_UNUSED(original);
            if (!normalized.startsWith(QStringLiteral("definir funcion")))
            {
                return false;
            }

            QString rest = normalized.mid(QStringLiteral("definir funcion").size()).trimmed();

            QRegularExpression multiParamRe(QStringLiteral("^([a-z ]+) ([a-zA-Z_][a-zA-Z0-9_]*) con parametro (.+)$"));
            QRegularExpressionMatch multiMatch = multiParamRe.match(rest);

            if (multiMatch.hasMatch())
            {
                QString returnTypePhrase = multiMatch.captured(1).trimmed();
                QString functionName = multiMatch.captured(2).trimmed();
                QString paramsText = multiMatch.captured(3).trimmed();

                QString returnType = typeFromPhrase(returnTypePhrase);

                FunctionInfo funcInfo;
                funcInfo.name = functionName;
                funcInfo.returnType = returnType;

                QStringList paramParts = paramsText.split(QStringLiteral(" y "));

                for (const QString &paramPart : paramParts)
                {
                    QString cleanParam = paramPart.trimmed();
                    QRegularExpression paramRe(QStringLiteral("^([a-z ]+) ([a-zA-Z_][a-zA-Z0-9_]*)$"));
                    QRegularExpressionMatch paramMatch = paramRe.match(cleanParam);

                    if (paramMatch.hasMatch())
                    {
                        QString paramTypePhrase = paramMatch.captured(1).trimmed();
                        QString paramName = paramMatch.captured(2).trimmed();

                        QString paramType = typeFromPhrase(paramTypePhrase);
                        QString paramIdentifier = sanitizedIdentifier(paramName);

                        funcInfo.parameterTypes << paramType;
                        funcInfo.parameterNames << paramIdentifier;

                        registerVariable(paramIdentifier, paramType, false);
                    }
                }

                m_functions.insert(functionName, funcInfo);
                m_insideFunction = true;
                m_currentFunctionName = functionName;
                return true;
            }

            QRegularExpression singleParamRe(QStringLiteral("^([a-z ]+) ([a-zA-Z_][a-zA-Z0-9_]*) con parametro ([a-z ]+) ([a-zA-Z_][a-zA-Z0-9_]*)$"));
            QRegularExpressionMatch singleMatch = singleParamRe.match(rest);
            if (!singleMatch.hasMatch())
            {
                return false;
            }

            QString returnTypePhrase = singleMatch.captured(1).trimmed();
            QString functionName = singleMatch.captured(2).trimmed();
            QString paramTypePhrase = singleMatch.captured(3).trimmed();
            QString paramName = singleMatch.captured(4).trimmed();

            QString returnType = typeFromPhrase(returnTypePhrase);
            QString paramType = typeFromPhrase(paramTypePhrase);
            QString paramIdentifier = sanitizedIdentifier(paramName);

            FunctionInfo funcInfo;
            funcInfo.name = functionName;
            funcInfo.returnType = returnType;
            funcInfo.parameterTypes << paramType;
            funcInfo.parameterNames << paramIdentifier;

            registerVariable(paramIdentifier, paramType, false);

            m_functions.insert(functionName, funcInfo);
            m_insideFunction = true;
            m_currentFunctionName = functionName;

            return true;
        }

        QString typeFromPhrase(const QString &phrase)
        {
            if (phrase.contains(QStringLiteral("numero entero")))
            {
                return QStringLiteral("int");
            }
            if (phrase.contains(QStringLiteral("numero decimal")))
            {
                return QStringLiteral("double");
            }
            if (phrase.contains(QStringLiteral("texto")))
            {
                return QStringLiteral("std::string");
            }
            if (phrase.contains(QStringLiteral("booleano")))
            {
                return QStringLiteral("bool");
            }
            return QStringLiteral("int");
        }

        bool handleReturnStatement(const QString &original, const QString &normalized)
        {
            Q_UNUSED(original);
            if (!normalized.startsWith(QStringLiteral("retornar")))
            {
                return false;
            }

            QString rest = normalized.mid(QStringLiteral("retornar").size()).trimmed();
            QString identifier = sanitizedIdentifier(rest);

            QString line = QStringLiteral("    return %1;").arg(identifier);

            if (m_insideFunction && !m_currentFunctionName.isEmpty())
            {
                int whileCount = 0;
                int braceCount = 0;

                for (const QString &bodyLine : m_functions[m_currentFunctionName].body)
                {
                    if (bodyLine.contains("while") && bodyLine.endsWith(" {"))
                    {
                        whileCount++;
                    }
                    if (bodyLine.trimmed() == QStringLiteral("    }"))
                    {
                        braceCount++;
                    }
                }

                if (whileCount > braceCount)
                {
                    for (int i = braceCount; i < whileCount; i++)
                    {
                        m_functions[m_currentFunctionName].body.append(QStringLiteral("    }"));
                    }
                }

                m_functions[m_currentFunctionName].body.append(line);

                const FunctionInfo &funcInfo = m_functions[m_currentFunctionName];
                for (const QString &paramName : funcInfo.parameterNames)
                {
                    m_variables.remove(paramName);
                }

                m_insideFunction = false;
                m_currentFunctionName.clear();
            }
            else
            {
                addCodeLine(line);
            }

            return true;
        }

        bool handleFunctionCall(const QString &original, const QString &normalized)
        {
            Q_UNUSED(original);

            QRegularExpression multiArgRe(QStringLiteral("^asignar valor a ([a-zA-Z_][a-zA-Z0-9_]*) con llamar funcion ([a-zA-Z_][a-zA-Z0-9_]*)\\(([^)]+)\\)$"));
            QRegularExpressionMatch multiMatch = multiArgRe.match(normalized);
            if (multiMatch.hasMatch())
            {
                QString varName = sanitizedIdentifier(multiMatch.captured(1));
                QString funcName = multiMatch.captured(2);
                QString argsText = multiMatch.captured(3).trimmed();

                if (!hasVariable(varName))
                {
                    addCodeLine(QStringLiteral("int %1;").arg(varName));
                    registerVariable(varName, QStringLiteral("int"), false);
                }

                QStringList argParts = argsText.contains(QStringLiteral(", ")) ? argsText.split(QStringLiteral(", ")) : argsText.split(QStringLiteral(" y "));

                QStringList cleanArgs;
                for (const QString &argPart : argParts)
                {
                    QString cleanArg = sanitizedIdentifier(argPart.trimmed());
                    if (!cleanArg.isEmpty())
                    {
                        cleanArgs << cleanArg;
                    }
                }

                addCodeLine(QStringLiteral("%1 = %2(%3);").arg(varName, funcName, cleanArgs.join(QStringLiteral(", "))));
                return true;
            }

            QRegularExpression callRe(QStringLiteral("^asignar valor a ([a-zA-Z_][a-zA-Z0-9_]*) con llamar funcion ([a-zA-Z_][a-zA-Z0-9_]*)\\(([a-zA-Z_][a-zA-Z0-9_]*)\\)$"));
            QRegularExpressionMatch callMatch = callRe.match(normalized);
            if (callMatch.hasMatch())
            {
                QString varName = sanitizedIdentifier(callMatch.captured(1));
                QString funcName = callMatch.captured(2);
                QString argName = sanitizedIdentifier(callMatch.captured(3));

                if (!hasVariable(varName))
                {
                    addCodeLine(QStringLiteral("int %1;").arg(varName));
                    registerVariable(varName, QStringLiteral("int"), false);
                }

                addCodeLine(QStringLiteral("%1 = %2(%3);").arg(varName, funcName, argName));
                return true;
            }
            return false;
        }

        bool handleVariableOperation(const QString &original, const QString &normalized)
        {
            Q_UNUSED(original);

            QRegularExpression multRe(QStringLiteral("^([a-zA-Z_][a-zA-Z0-9_]*) multiplicar por ([a-zA-Z_][a-zA-Z0-9_]*)$"));
            QRegularExpressionMatch multMatch = multRe.match(normalized);
            if (multMatch.hasMatch())
            {
                QString var1 = sanitizedIdentifier(multMatch.captured(1));
                QString var2 = sanitizedIdentifier(multMatch.captured(2));

                QString line = QStringLiteral("    %1 *= %2;").arg(var1, var2);

                if (m_insideFunction && !m_currentFunctionName.isEmpty())
                {
                    m_functions[m_currentFunctionName].body.append(line);
                }
                else
                {
                    addCodeLine(QStringLiteral("%1 *= %2;").arg(var1, var2));
                }
                return true;
            }

            QRegularExpression subRe(QStringLiteral("^([a-zA-Z_][a-zA-Z0-9_]*) restar (\\d+)$"));
            QRegularExpressionMatch subMatch = subRe.match(normalized);
            if (subMatch.hasMatch())
            {
                QString varName = sanitizedIdentifier(subMatch.captured(1));
                QString number = subMatch.captured(2);

                QString line = QStringLiteral("    %1 -= %2;").arg(varName, number);

                if (m_insideFunction && !m_currentFunctionName.isEmpty())
                {
                    m_functions[m_currentFunctionName].body.append(line);
                }
                else
                {
                    addCodeLine(QStringLiteral("%1 -= %2;").arg(varName, number));
                }
                return true;
            }

            return false;
        }

        bool handleCalculateExpression(const QString &original, const QString &normalized)
        {
            if (!normalized.startsWith(QStringLiteral("calcular ")))
            {
                return false;
            }

            QString rest = normalized.mid(QStringLiteral("calcular ").size()).trimmed();

            int idxAsignar = rest.indexOf(QStringLiteral(" y asignar a "));
            int idxAsignarAl = rest.indexOf(QStringLiteral(" y asignar al "));

            QString assignToken;
            if (idxAsignar >= 0)
            {
                assignToken = QStringLiteral(" y asignar a ");
            }
            else if (idxAsignarAl >= 0)
            {
                assignToken = QStringLiteral(" y asignar al ");
                idxAsignar = idxAsignarAl;
            }
            else
            {
                return false;
            }

            QString exprPart = rest.left(idxAsignar).trimmed();
            QString destPart = rest.mid(idxAsignar + assignToken.size()).trimmed();

            if (exprPart.isEmpty() || destPart.isEmpty())
            {
                return false;
            }

            int idxComo = exprPart.indexOf(QStringLiteral(" como "));
            if (idxComo >= 0)
            {
                exprPart = exprPart.mid(idxComo + QStringLiteral(" como ").size()).trimmed();
            }

            QString dest = sanitizedIdentifier(destPart);
            if (dest.isEmpty())
            {
                notifyIssue(QStringLiteral("No se pudo interpretar la variable destino en la instrucción de cálculo."));
                return true;
            }

            QString expr = translateExpression(exprPart, original);
            if (expr.isEmpty())
            {
                notifyIssue(QStringLiteral("No se pudo interpretar la expresión a calcular: %1").arg(exprPart));
                return true;
            }

            if (!hasVariable(dest))
            {
                QString varType = QStringLiteral("int");
                QString defaultValue = QStringLiteral("0");

                if (expr.contains('.') ||
                    exprPart.contains(QStringLiteral("decimal")) ||
                    expr.contains('/') ||
                    exprPart.contains(QStringLiteral("dividir")) ||
                    exprPart.contains(QStringLiteral("dividido")))
                {
                    varType = QStringLiteral("double");
                    defaultValue = QStringLiteral("0.0");
                }

                ensureVariable(dest, varType, defaultValue);
            }

            QString assignmentLine = QStringLiteral("%1 = %2;").arg(dest, expr);

            if (m_insideFunction && !m_currentFunctionName.isEmpty())
            {
                m_functions[m_currentFunctionName].body.append(QStringLiteral("    %1").arg(assignmentLine));
            }
            else
            {
                addCodeLine(assignmentLine);
            }

            return true;
        }

        bool handleCalculateAverage(const QString &original, const QString &normalized)
        {
            Q_UNUSED(original);

            if ((normalized.contains(QStringLiteral("calcular promedio")) ||
                 normalized.contains(QStringLiteral("calcular el promedio"))) &&
                normalized.contains(QStringLiteral("como")) &&
                normalized.contains(QStringLiteral("dividir")))
            {

                return false;
            }

            return false;
        }

        bool handleInputValue(const QString &original, const QString &normalized)
        {
            Q_UNUSED(original);
            QString core;
            if (normalized.startsWith(QStringLiteral("ingresar valor")))
            {
                core = normalized.mid(QStringLiteral("ingresar valor").size()).trimmed();
            }
            else if (normalized.startsWith(QStringLiteral("ingresar los valores")))
            {
                core = normalized.mid(QStringLiteral("ingresar los valores").size()).trimmed();
            }
            else if (normalized.startsWith(QStringLiteral("ingresar")))
            {
                core = normalized.mid(QStringLiteral("ingresar").size()).trimmed();
            }
            else
            {
                return false;
            }

            if (core.isEmpty())
            {
                notifyIssue(QStringLiteral("Se solicitó ingresar un valor, pero no se indicó la variable."));
                return true;
            }

            if (core.startsWith(QStringLiteral("de la ")) || core.startsWith(QStringLiteral("del ")))
            {
                QString remainder = core.startsWith(QStringLiteral("de la ")) ? core.mid(QStringLiteral("de la ").size()).trimmed() : core.mid(QStringLiteral("del ").size()).trimmed();

                if (remainder == QStringLiteral("lista") ||
                    remainder == QStringLiteral("vector") ||
                    remainder == QStringLiteral("arreglo"))
                {
                    return requestInputForCollection(remainder);
                }
            }

            QRegularExpression eachElementRe(QStringLiteral("^de cada (.+) en (?:la|el) (lista|vector|arreglo)$"));
            QRegularExpressionMatch eachElementMatch = eachElementRe.match(core);
            if (eachElementMatch.hasMatch())
            {
                QString alias = eachElementMatch.captured(2);
                return requestInputForCollection(alias);
            }

            QString identifier = sanitizedIdentifier(core);
            if (identifier.isEmpty())
            {
                notifyIssue(QStringLiteral("No se pudo interpretar la variable para ingresar datos."));
                return true;
            }

            if (!hasVariable(identifier))
            {
                addCodeLine(QStringLiteral("int %1;").arg(identifier));
                registerVariable(identifier, QStringLiteral("int"), false);
            }

            ensureInclude("iostream");

            QString prompt;
            if (identifier == QStringLiteral("x"))
            {
                prompt = QStringLiteral("Ingrese un número: ");
            }
            else if (identifier == QStringLiteral("edad"))
            {
                prompt = QStringLiteral("Ingrese la edad: ");
            }
            else
            {
                prompt = QStringLiteral("Ingrese el %1: ").arg(identifier);
            }

            addCodeLine(QStringLiteral("std::cout << \"%1\";").arg(prompt));
            addCodeLine(QStringLiteral("std::cin >> %1;").arg(identifier));
            return true;
        }

        QString translateExpression(const QString &valuePart, const QString &original)
        {
            Q_UNUSED(original);
            QString expr = valuePart;
            QString normalizedExpr = normalizeLine(expr);

            if (normalizedExpr == QStringLiteral("verdadero") || normalizedExpr == QStringLiteral("true"))
            {
                return QStringLiteral("true");
            }
            if (normalizedExpr == QStringLiteral("falso") || normalizedExpr == QStringLiteral("false"))
            {
                return QStringLiteral("false");
            }

            normalizedExpr.replace(QStringLiteral(" mas "), QStringLiteral(" + "));
            normalizedExpr.replace(QStringLiteral(" mas"), QStringLiteral(" +"));
            normalizedExpr.replace(QStringLiteral("mas "), QStringLiteral("+ "));
            normalizedExpr.replace(QStringLiteral("menos"), QStringLiteral("-"));
            normalizedExpr.replace(QStringLiteral(" multiplicado por "), QStringLiteral(" * "));
            normalizedExpr.replace(QStringLiteral(" dividido entre "), QStringLiteral(" / "));

            if (normalizedExpr.contains(QStringLiteral("total dividido entre")))
            {
                QRegularExpression re(QString::fromLatin1(R"(([a-zA-Z_][a-zA-Z0-9_]*)\s+dividido entre\s+(-?\d+(?:[.,]\d+)?))"));
                QRegularExpressionMatch match = re.match(normalizedExpr);
                if (match.hasMatch())
                {
                    QString varName = sanitizedIdentifier(match.captured(1));
                    QString number = ensureNumberString(match.captured(2), true);
                    return QStringLiteral("%1 / %2").arg(varName, number);
                }
            }

            if (normalizedExpr.contains(QStringLiteral("total")) && normalizedExpr.contains(QStringLiteral(" entre ")))
            {
                QStringList parts = normalizedExpr.split(QStringLiteral(" entre "));
                if (parts.size() == 2)
                {
                    QString left = sanitizedIdentifier(parts[0].trimmed());
                    QString right = ensureNumberString(parts[1], true);
                    return QStringLiteral("%1 / %2").arg(left, right);
                }
            }

            QRegularExpression simpleDivide(QStringLiteral("^([a-zA-Z_][a-zA-Z0-9_]*)\\s+dividir\\s+(.+)$"));
            QRegularExpressionMatch simpleMatch = simpleDivide.match(normalizedExpr);
            if (simpleMatch.hasMatch())
            {
                QString left = sanitizedIdentifier(simpleMatch.captured(1));
                QString rightToken = simpleMatch.captured(2).trimmed();
                QString right;
                if (QRegularExpression(QStringLiteral("^-?\\d+(?:[\\.,]\\d+)?$"))
                        .match(rightToken)
                        .hasMatch())
                {
                    right = ensureNumberString(rightToken, rightToken.contains('.') || rightToken.contains(','));
                }
                else
                {
                    right = sanitizedIdentifier(rightToken);
                }
                if (left.isEmpty() || right.isEmpty())
                {
                    return QString();
                }
                return QStringLiteral("%1 / %2").arg(left, right);
            }

            if (normalizedExpr.contains(QStringLiteral(" dividido entre ")))
            {
                QStringList parts = normalizedExpr.split(QStringLiteral(" dividido entre "));
                if (parts.size() == 2)
                {
                    QString left = sanitizedIdentifier(parts[0].trimmed());
                    QString rightPart = parts[1].trimmed();
                    bool isDecimal = isDecimalNumber(rightPart);
                    QString right = ensureNumberString(rightPart, isDecimal);
                    return QStringLiteral("%1 / %2").arg(left, right);
                }
            }

            QRegularExpression numbers("-?\\d+(?:[\\.,]\\d+)?");
            QString processed = normalizedExpr;
            int idx = 0;
            while (true)
            {
                QRegularExpressionMatch match = numbers.match(processed, idx);
                if (!match.hasMatch())
                {
                    break;
                }
                QString number = match.captured();
                QString replacement = ensureNumberString(number, number.contains('.') || number.contains(','));
                processed.replace(match.capturedStart(), match.capturedLength(), replacement);
                idx = match.capturedStart() + replacement.size();
            }

            QRegularExpression variablePattern(QStringLiteral("\\b([a-zA-Z_][a-zA-Z0-9_]*)\\b"));
            QRegularExpressionMatchIterator varIterator = variablePattern.globalMatch(processed);
            QString finalProcessed = processed;

            while (varIterator.hasNext())
            {
                QRegularExpressionMatch varMatch = varIterator.next();
                QString varCandidate = varMatch.captured(1);
                QString sanitizedVar = sanitizedIdentifier(varCandidate);

                if (hasVariable(sanitizedVar) && varCandidate != sanitizedVar)
                {
                    finalProcessed.replace(varCandidate, sanitizedVar);
                }
            }

            finalProcessed.replace(QStringLiteral(" "), QString());
            return finalProcessed;
        }

        QString literalForType(const QString &valueText,
                               const QString &original,
                               const QString &type)
        {
            QString trimmed = valueText.trimmed();

            if (type == QStringLiteral("std::string"))
            {
                ensureInclude("string");
                QString quotedText = readQuotedText(original);
                if (quotedText.isEmpty())
                {
                    quotedText = trimmed;
                }
                return quoted(quotedText);
            }

            if (type == QStringLiteral("bool"))
            {
                if (trimmed == QStringLiteral("verdadero") || trimmed == QStringLiteral("true"))
                {
                    return QStringLiteral("true");
                }
                if (trimmed == QStringLiteral("falso") || trimmed == QStringLiteral("false"))
                {
                    return QStringLiteral("false");
                }
                QString identifier = sanitizedIdentifier(trimmed);
                if (hasVariable(identifier))
                {
                    return identifier;
                }
                return QStringLiteral("false");
            }

            bool hasDecimal = trimmed.contains('.') || trimmed.contains(',');
            bool expectFloating = (type == QStringLiteral("double")) || hasDecimal;
            if (type == QStringLiteral("double") || type == QStringLiteral("int"))
            {
                QRegularExpression numberRegex(QStringLiteral(R"(-?\d+(?:[.,]\d+)?)"));
                QRegularExpressionMatch match = numberRegex.match(trimmed);
                if (match.hasMatch())
                {
                    return ensureNumberString(match.captured(), expectFloating);
                }
                QString identifier = sanitizedIdentifier(trimmed);
                if (!identifier.isEmpty())
                {
                    return identifier;
                }
                notifyIssue(QStringLiteral("No se pudo interpretar el valor numérico: %1").arg(valueText));
                return expectFloating ? QStringLiteral("0.0") : QStringLiteral("0");
            }

            QString identifier = sanitizedIdentifier(trimmed);
            if (identifier.isEmpty())
            {
                notifyIssue(QStringLiteral("No se pudo interpretar el valor '%1' para la colección").arg(valueText));
            }
            return identifier.isEmpty() ? trimmed : identifier;
        }

        bool handleArithmeticBinary(const QString &original, const QString &normalized)
        {
            Q_UNUSED(original);
            static const struct
            {
                QString verb;
                QString op;
                bool useEntre;
            } entries[] = {
                {QStringLiteral("sumar"), QStringLiteral("+"), false},
                {QStringLiteral("restar"), QStringLiteral("-"), false},
                {QStringLiteral("multiplicar"), QStringLiteral("*"), false},
                {QStringLiteral("dividir"), QStringLiteral("/"), true}};

            auto operandInfo = [](const QString &token)
            {
                QString trimmed = token.trimmed();
                QRegularExpression numberRegex(QStringLiteral(R"(^-?\d+(?:[.,]\d+)?$)"));
                if (numberRegex.match(trimmed).hasMatch())
                {
                    bool hasDecimal = trimmed.contains('.') || trimmed.contains(',');
                    return QPair<QString, bool>(ensureNumberString(trimmed, hasDecimal), hasDecimal);
                }
                QString identifier = sanitizedIdentifier(trimmed);
                return QPair<QString, bool>(identifier, false);
            };

            for (const auto &entry : entries)
            {
                if (!normalized.startsWith(entry.verb + QLatin1Char(' ')))
                {
                    continue;
                }

                QString tail = normalized.mid(entry.verb.size()).trimmed();
                QString leftToken;
                QString rightToken;

                if (entry.useEntre)
                {
                    int idx = tail.indexOf(QStringLiteral(" entre "));
                    if (idx < 0)
                    {
                        continue;
                    }
                    leftToken = tail.left(idx).trimmed();
                    rightToken = tail.mid(idx + QStringLiteral(" entre ").size()).trimmed();
                }
                else
                {
                    int idx = tail.indexOf(QStringLiteral(" y "));
                    if (idx < 0)
                    {
                        continue;
                    }
                    leftToken = tail.left(idx).trimmed();
                    rightToken = tail.mid(idx + QStringLiteral(" y ").size()).trimmed();
                }

                auto leftInfo = operandInfo(leftToken);
                auto rightInfo = operandInfo(rightToken);

                if (leftInfo.first.isEmpty() || rightInfo.first.isEmpty())
                {
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

        bool handleArithmeticAggregate(const QString &original, const QString &normalized)
        {
            Q_UNUSED(original);
            if (!normalized.startsWith(QStringLiteral("sumar los numeros")))
            {
                return false;
            }

            QRegularExpression numberRegex(QStringLiteral(R"(-?\d+(?:[.,]\d+)?)"));
            QRegularExpressionMatchIterator it = numberRegex.globalMatch(normalized);
            QVector<QString> numbers;
            bool anyDecimal = false;
            while (it.hasNext())
            {
                QRegularExpressionMatch match = it.next();
                QString raw = match.captured();
                bool hasDecimal = raw.contains('.') || raw.contains(',');
                numbers.append(ensureNumberString(raw, hasDecimal));
                anyDecimal = anyDecimal || hasDecimal;
            }

            if (numbers.isEmpty())
            {
                return false;
            }

            QString type = anyDecimal ? QStringLiteral("double") : QStringLiteral("int");
            QString initial = anyDecimal ? QStringLiteral("0.0") : QStringLiteral("0");
            QString accumulator = QStringLiteral("suma%1").arg(m_tempCounter++);
            addCodeLine(QStringLiteral("%1 %2 = %3;").arg(type, accumulator, initial));
            for (const QString &num : numbers)
            {
                addCodeLine(QStringLiteral("%1 += %2;").arg(accumulator, num));
            }
            addCodeLine(QStringLiteral("std::cout << %1 << std::endl;").arg(accumulator));
            ensureInclude("iostream");
            return true;
        }

        bool handleCompoundArithmeticInstruction(const QString &original, const QString &normalized)
        {
            Q_UNUSED(original);
            if (normalized.startsWith(QStringLiteral("sumar los numeros")) &&
                normalized.contains(QStringLiteral("y mostrar el resultado")))
            {

                QString numbersOnly = normalized;
                int endIdx = numbersOnly.indexOf(QStringLiteral("y mostrar el resultado"));
                if (endIdx > 0)
                {
                    numbersOnly = numbersOnly.left(endIdx).trimmed();
                }

                QRegularExpression numberRegex(QStringLiteral(R"(-?\d+(?:[.,]\d+)?)"));
                QRegularExpressionMatchIterator it = numberRegex.globalMatch(numbersOnly);
                QVector<QString> numbers;
                bool anyDecimal = false;
                while (it.hasNext())
                {
                    QRegularExpressionMatch match = it.next();
                    QString raw = match.captured();
                    bool hasDecimal = raw.contains('.') || raw.contains(',');
                    numbers.append(ensureNumberString(raw, hasDecimal));
                    anyDecimal = anyDecimal || hasDecimal;
                }

                if (numbers.isEmpty())
                {
                    return false;
                }

                QString type = anyDecimal ? QStringLiteral("double") : QStringLiteral("int");
                QString initial = anyDecimal ? QStringLiteral("0.0") : QStringLiteral("0");
                QString accumulator = QStringLiteral("resultado%1").arg(m_tempCounter++);

                addCodeLine(QStringLiteral("%1 %2 = %3;").arg(type, accumulator, initial));
                for (const QString &num : numbers)
                {
                    addCodeLine(QStringLiteral("%1 += %2;").arg(accumulator, num));
                }

                ensureInclude("iostream");
                addCodeLine(QStringLiteral("std::cout << %1 << std::endl;").arg(accumulator));

                return true;
            }
            return false;
        }

        bool handleRepeatMessage(const QString &original, const QString &normalized)
        {
            if (!normalized.startsWith(QStringLiteral("repetir")))
            {
                return false;
            }

            QRegularExpression re(QString::fromLatin1(R"(^repetir\s+(\d+)\s+veces\s+(mostrar|imprimir))"));
            QRegularExpressionMatch match = re.match(normalized);
            if (!match.hasMatch())
            {
                return false;
            }

            QString times = match.captured(1);
            QString message = readQuotedText(original);
            if (message.isEmpty())
            {
                QRegularExpression messageRe(QString::fromLatin1(R"(^repetir\s+\d+\s+veces\s+(?:mostrar|imprimir)\s+(?:el mensaje\s+)?(.+)$)"));
                QRegularExpressionMatch messageMatch = messageRe.match(normalized);
                if (messageMatch.hasMatch())
                {
                    message = messageMatch.captured(1).trimmed();
                    if (message.startsWith('"') && message.endsWith('"') && message.size() >= 2)
                    {
                        message = message.mid(1, message.size() - 2);
                    }
                }
            }

            if (message.isEmpty())
            {
                return false;
            }

            QString literal = quoted(message);
            ensureInclude("iostream");

            QString counter = QStringLiteral("i");
            if (hasVariable(counter))
            {
                counter = QStringLiteral("i%1").arg(m_tempCounter++);
            }
            addCodeLine(QStringLiteral("for (int %1 = 0; %1 < %2; ++%1) {").arg(counter, times));
            ++m_indentLevel;
            addCodeLine(QStringLiteral("std::cout << %1 << std::endl;").arg(literal));
            --m_indentLevel;
            addCodeLine(QStringLiteral("}"));
            return true;
        }

        bool handleWhileIncrease(const QString &original, const QString &normalized)
        {
            Q_UNUSED(original);

            QRegularExpression whileRe(QStringLiteral("^mientras ([a-zA-Z_][a-zA-Z0-9_]*) (mayor que|menor que|igual a) (\\d+)$"));
            QRegularExpressionMatch whileMatch = whileRe.match(normalized);
            if (whileMatch.hasMatch())
            {
                QString varName = sanitizedIdentifier(whileMatch.captured(1));
                QString op = whileMatch.captured(2);
                QString value = whileMatch.captured(3);

                QString cppOp;
                if (op == QStringLiteral("mayor que"))
                {
                    cppOp = QStringLiteral(">");
                }
                else if (op == QStringLiteral("menor que"))
                {
                    cppOp = QStringLiteral("<");
                }
                else if (op == QStringLiteral("igual a"))
                {
                    cppOp = QStringLiteral("==");
                }

                QString line = QStringLiteral("while (%1 %2 %3) {").arg(varName, cppOp, value);

                if (m_insideFunction && !m_currentFunctionName.isEmpty())
                {
                    m_functions[m_currentFunctionName].body.append(line);
                }
                else
                {
                    startBlock(QStringLiteral("while (%1 %2 %3) {").arg(varName, cppOp, value), BlockType::Loop, true, m_currentIndent);
                }
                return true;
            }

            QRegularExpression re("^mientras el ([a-zA-Z_]+) sea menor que (-?\\d+(?:[\\.,]\\d+)?) sumar (-?\\d+(?:[\\.,]\\d+)?) al \\1$");
            QRegularExpressionMatch match = re.match(normalized);
            if (!match.hasMatch())
            {
                return false;
            }

            QString variable = sanitizedIdentifier(match.captured(1));
            QString limitStr = match.captured(2);
            QString incrementStr = match.captured(3);

            QString variableType = determineNumericType(limitStr, incrementStr);
            bool isFloating = (variableType == QStringLiteral("double"));

            QString limit = ensureNumberString(limitStr, isFloating);
            QString increment = ensureNumberString(incrementStr, isFloating);
            QString defaultValue = getDefaultValueForType(variableType);

            ensureVariable(variable, variableType, defaultValue);

            startBlock(QStringLiteral("while (%1 < %2) {").arg(variable, limit), BlockType::Loop, false, m_currentIndent);
            addCodeLine(QStringLiteral("%1 += %2;").arg(variable, increment));
            return true;
        }

        bool handleCreateCollection(const QString &original, const QString &normalized)
        {
            Q_UNUSED(original);
            if (!normalized.startsWith(QStringLiteral("crear")))
            {
                return false;
            }

            QRegularExpression reNumWithElements("^crear (?:una |un )?(lista|vector|arreglo) de (numeros? [a-z]+) con (\\d+) elementos$");
            QRegularExpressionMatch mNumWithElements = reNumWithElements.match(normalized);
            if (mNumWithElements.hasMatch())
            {
                QString alias = mNumWithElements.captured(1);
                QString elementPhrase = mNumWithElements.captured(2).trimmed();
                int size = mNumWithElements.captured(3).toInt();
                QString elementType = elementTypeFromPhrase(elementPhrase);
                QString aliasToken = sanitizedIdentifier(alias);
                if (aliasToken.isEmpty())
                {
                    aliasToken = QStringLiteral("lista");
                }

                QString baseName = aliasToken;
                QString variableName = uniqueName(baseName);
                QString type = QStringLiteral("std::vector<%1>").arg(elementType);
                ensureInclude("vector");
                if (elementType == QStringLiteral("std::string"))
                {
                    ensureInclude("string");
                }
                addCodeLine(QStringLiteral("%1 %2(%3);").arg(type, variableName).arg(size));
                registerCollection(variableName, {type, elementType, aliasToken, size, false, false});
                return true;
            }

            QRegularExpression reNumPattern("^crear (?:una |un )?(lista|vector|arreglo) de (\\d+) (numeros? [a-z]+)$");
            QRegularExpressionMatch mNum = reNumPattern.match(normalized);
            if (mNum.hasMatch())
            {
                QString alias = mNum.captured(1);
                int size = mNum.captured(2).toInt();
                QString elementPhrase = mNum.captured(3).trimmed();
                QString elementType = elementTypeFromPhrase(elementPhrase);
                QString aliasToken = sanitizedIdentifier(alias);
                if (aliasToken.isEmpty())
                {
                    aliasToken = QStringLiteral("lista");
                }

                QString baseName = aliasToken;
                QString variableName = uniqueName(baseName);
                QString type = QStringLiteral("std::vector<%1>").arg(elementType);
                ensureInclude("vector");
                if (elementType == QStringLiteral("std::string"))
                {
                    ensureInclude("string");
                }
                addCodeLine(QStringLiteral("%1 %2(%3);").arg(type, variableName).arg(size));
                registerCollection(variableName, {type, elementType, aliasToken, size, false, false});
                return true;
            }

            QRegularExpression reSize("^crear (?:una |un )?(lista|vector|arreglo) de (?:\\d+ )?([a-z ]+) con (\\d+) elementos$");
            QRegularExpressionMatch m = reSize.match(normalized);
            if (m.hasMatch())
            {
                QString alias = m.captured(1);
                QString elementPhrase = m.captured(2).trimmed();
                int size = m.captured(3).toInt();
                QString elementType = elementTypeFromPhrase(elementPhrase);
                QString aliasToken = sanitizedIdentifier(alias);
                if (aliasToken.isEmpty())
                {
                    aliasToken = QStringLiteral("lista");
                }

                if (aliasToken == QStringLiteral("arreglo"))
                {
                    QString variableName = uniqueName(QStringLiteral("arreglo"));
                    addCodeLine(QStringLiteral("%1 %2[%3];").arg(elementType, variableName).arg(size));
                    registerCollection(variableName, {elementType, elementType, aliasToken, size, true, true});
                    return true;
                }

                QString baseName = aliasToken == QStringLiteral("vector") ? QStringLiteral("vector") : QStringLiteral("lista");
                QString variableName = uniqueName(baseName);
                QString type = QStringLiteral("std::vector<%1>").arg(elementType);
                ensureInclude("vector");
                if (elementType == QStringLiteral("std::string"))
                {
                    ensureInclude("string");
                }
                addCodeLine(QStringLiteral("%1 %2(%3);").arg(type, variableName).arg(size));
                registerCollection(variableName, {type, elementType, aliasToken, size, false, false});
                return true;
            }

            QRegularExpression reSizeShort("^crear (?:una |un )?(lista|vector|arreglo) de (\\d+) ([a-z ]+)$");
            QRegularExpressionMatch mShort = reSizeShort.match(normalized);
            if (mShort.hasMatch())
            {
                QString alias = mShort.captured(1);
                int size = mShort.captured(2).toInt();
                QString elementPhrase = mShort.captured(3).trimmed();
                QString elementType = elementTypeFromPhrase(elementPhrase);
                QString aliasToken = sanitizedIdentifier(alias);
                if (aliasToken.isEmpty())
                {
                    aliasToken = QStringLiteral("lista");
                }

                if (aliasToken == QStringLiteral("arreglo"))
                {
                    QString variableName = uniqueName(QStringLiteral("arreglo"));
                    addCodeLine(QStringLiteral("%1 %2[%3];").arg(elementType, variableName).arg(size));
                    registerCollection(variableName, {elementType, elementType, aliasToken, size, true, true});
                    return true;
                }

                QString baseName = aliasToken == QStringLiteral("vector") ? QStringLiteral("vector") : QStringLiteral("lista");
                QString variableName = uniqueName(baseName);
                QString type = QStringLiteral("std::vector<%1>").arg(elementType);
                ensureInclude("vector");
                if (elementType == QStringLiteral("std::string"))
                {
                    ensureInclude("string");
                }
                addCodeLine(QStringLiteral("%1 %2(%3);").arg(type, variableName).arg(size));
                registerCollection(variableName, {type, elementType, aliasToken, size, false, false});
                return true;
            }

            QRegularExpression reStore("^crear (?:una |un )?lista de texto para guardar (?:los |las )?([a-z\u00e1\u00e9\u00ed\u00f3\u00fa\u00fc\u00f1 ]+)$");
            QRegularExpressionMatch mStore = reStore.match(normalized);
            if (mStore.hasMatch())
            {
                QString aliasWord = mStore.captured(1).trimmed();
                QString variableName = sanitizedIdentifier(aliasWord);
                if (variableName.isEmpty())
                {
                    variableName = QStringLiteral("lista");
                }
                QString aliasToken = sanitizedIdentifier(aliasWord);
                if (aliasToken.isEmpty())
                {
                    aliasToken = QStringLiteral("lista");
                }
                QString type = QStringLiteral("std::vector<std::string>");
                ensureInclude("vector");
                ensureInclude("string");
                addCodeLine(QStringLiteral("%1 %2;").arg(type, variableName));
                registerCollection(variableName, {type, QStringLiteral("std::string"), aliasToken, 0, false, false});
                return true;
            }

            QRegularExpression reStoreNum("^crear (?:una |un )?lista de (?:numeros? )?(?:decimales?|enteros?) para guardar (?:los |las )?([a-z\u00e1\u00e9\u00ed\u00f3\u00fa\u00fc\u00f1 ]+)$");
            QRegularExpressionMatch mStoreNum = reStoreNum.match(normalized);
            if (mStoreNum.hasMatch())
            {
                QString aliasWord = mStoreNum.captured(1).trimmed();
                QString variableName = sanitizedIdentifier(aliasWord);
                if (variableName.isEmpty())
                {
                    variableName = QStringLiteral("lista");
                }
                QString aliasToken = sanitizedIdentifier(aliasWord);
                if (aliasToken.isEmpty())
                {
                    aliasToken = QStringLiteral("lista");
                }

                QString elementType;
                QString type;
                if (normalized.contains(QStringLiteral("decimal")))
                {
                    elementType = QStringLiteral("double");
                    type = QStringLiteral("std::vector<double>");
                }
                else
                {
                    elementType = QStringLiteral("int");
                    type = QStringLiteral("std::vector<int>");
                }

                ensureInclude("vector");
                addCodeLine(QStringLiteral("%1 %2;").arg(type, variableName));
                registerCollection(variableName, {type, elementType, aliasToken, 0, false, false});
                return true;
            }

            QRegularExpression reVector("^crear un vector de ([a-z ]+) con (\\d+) elementos$");
            QRegularExpressionMatch mVec = reVector.match(normalized);
            if (mVec.hasMatch())
            {
                QString elementPhrase = mVec.captured(1).trimmed();
                int size = mVec.captured(2).toInt();
                QString elementType = elementTypeFromPhrase(elementPhrase);
                QString variableName = QStringLiteral("vector");
                variableName = uniqueName(variableName);
                QString type = QStringLiteral("std::vector<%1>").arg(elementType);
                ensureInclude("vector");
                if (elementType == QStringLiteral("std::string"))
                {
                    ensureInclude("string");
                }
                addCodeLine(QStringLiteral("%1 %2(%3);").arg(type, variableName).arg(size));
                registerCollection(variableName, {type, elementType, QStringLiteral("vector"), size, false, false});
                return true;
            }

            return false;
        }

        bool isDecimalNumber(const QString &numberStr)
        {
            return numberStr.contains('.') || numberStr.contains(',');
        }

        QString determineNumericType(const QString &num1, const QString &num2 = QString())
        {
            bool hasDecimal = isDecimalNumber(num1);
            if (!num2.isEmpty())
            {
                hasDecimal = hasDecimal || isDecimalNumber(num2);
            }
            return hasDecimal ? QStringLiteral("double") : QStringLiteral("int");
        }

        QString getDefaultValueForType(const QString &type)
        {
            return (type == QStringLiteral("double")) ? QStringLiteral("0.0") : QStringLiteral("0");
        }

        QString uniqueName(const QString &base)
        {
            QString candidate = base;
            int suffix = 1;
            while (m_variables.contains(candidate) || m_collections.contains(candidate))
            {
                candidate = base + QString::number(++suffix);
            }
            return candidate;
        }

        QString elementTypeFromPhrase(const QString &phrase)
        {
            if (phrase.contains(QStringLiteral("texto")) || phrase.contains(QStringLiteral("cadena")))
            {
                ensureInclude("string");
                return QStringLiteral("std::string");
            }
            if (phrase.contains(QStringLiteral("decimal")) || phrase.contains(QStringLiteral("decimales")))
            {
                return QStringLiteral("double");
            }
            if (phrase.contains(QStringLiteral("entero")) || phrase.contains(QStringLiteral("enteros")) ||
                phrase.contains(QStringLiteral("numeros enteros")) || phrase.contains(QStringLiteral("numero entero")))
            {
                return QStringLiteral("int");
            }
            if (phrase.contains(QStringLiteral("numero")) || phrase.contains(QStringLiteral("numeros")))
            {
                return QStringLiteral("int");
            }
            return QStringLiteral("std::string");
        }

        bool handleAssignCollectionElement(const QString &original, const QString &normalized)
        {
            if (!normalized.startsWith(QStringLiteral("asignar valor")))
            {
                return false;
            }

            QRegularExpression re(QStringLiteral("^asignar valor (.+) al (primer|segundo|tercer|cuarto|quinto|sexto|septimo|octavo|noveno|decimo) elemento de la (lista|vector|arreglo)$"));
            QRegularExpressionMatch match = re.match(normalized);
            if (!match.hasMatch())
            {
                return false;
            }

            QString value = match.captured(1).trimmed();
            QString ordinal = match.captured(2);
            QString alias = match.captured(3);

            QString collectionName = collectionNameForAlias(alias);
            if (collectionName.isEmpty())
            {
                collectionName = lastCollection();
            }
            if (collectionName.isEmpty())
            {
                notifyIssue(QStringLiteral("No se encontró ninguna colección disponible para esta instrucción."));
                return false;
            }

            int index = ordinalToIndex(ordinal);
            QString elementType = elementTypeForCollection(collectionName);
            const auto info = m_collections.value(collectionName);
            if (info.isCArray)
            {
                if (index < 0 || index >= info.size)
                {
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

        int ordinalToIndex(const QString &ordinal) const
        {
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
                {QStringLiteral("decimo"), 9}};
            if (ordinal == QStringLiteral("ultimo"))
            {
                return -1;
            }
            return ordinals.value(ordinal, 0);
        }

        bool handleAddToCollection(const QString &original, const QString &normalized)
        {
            QRegularExpression re(QStringLiteral("^(agregar|agrega|anadir|anade) (.+) (?:a|al|a la|a el) (lista|vector|arreglo)$"));
            QRegularExpressionMatch match = re.match(normalized);
            if (!match.hasMatch())
            {
                return false;
            }

            QString valueText = match.captured(2).trimmed();
            QString alias = match.captured(3);

            QString collectionName = collectionNameForAlias(alias);
            if (collectionName.isEmpty())
            {
                collectionName = lastCollection();
            }
            if (collectionName.isEmpty())
            {
                notifyIssue(QStringLiteral("No se encontró ninguna colección disponible para esta instrucción."));
                return false;
            }

            const auto info = m_collections.value(collectionName);
            if (info.isCArray)
            {
                notifyIssue(QStringLiteral("No se pueden agregar elementos adicionales al arreglo %1."));
                return true;
            }

            QString elementType = info.elementType;
            if (elementType.isEmpty())
            {
                notifyIssue(QStringLiteral("No se pudo determinar el tipo de datos de la colección."));
                return false;
            }
            QString valueExpr = literalForType(valueText, original, elementType);

            ensureInclude("vector");
            addCodeLine(QStringLiteral("%1.push_back(%2);").arg(collectionName, valueExpr));
            auto itAdd = m_collections.find(collectionName);
            if (itAdd != m_collections.end())
            {
                if (itAdd->size >= 0)
                {
                    itAdd->size += 1;
                }
            }
            return true;
        }

        bool handleRemoveFromCollection(const QString &original, const QString &normalized)
        {
            Q_UNUSED(original);
            QRegularExpression re(QStringLiteral("^(eliminar|quitar) el (primer|segundo|tercer|cuarto|quinto|sexto|septimo|octavo|noveno|decimo|ultimo) elemento de (?:la|el|del) (lista|vector|arreglo)$"));
            QRegularExpressionMatch match = re.match(normalized);
            if (!match.hasMatch())
            {
                return false;
            }

            QString ordinal = match.captured(2);
            QString alias = match.captured(3);
            QString collectionName = collectionNameForAlias(alias);
            if (collectionName.isEmpty())
            {
                collectionName = lastCollection();
            }
            if (collectionName.isEmpty())
            {
                notifyIssue(QStringLiteral("No se encontró ninguna colección disponible para esta instrucción."));
                return false;
            }

            const auto info = m_collections.value(collectionName);
            if (info.isCArray)
            {
                notifyIssue(QStringLiteral("No se puede eliminar elementos en un arreglo de tamaño fijo."));
                return true;
            }

            ensureInclude("vector");

            int index = ordinalToIndex(ordinal);
            if (index >= 0)
            {
                int knownSize = collectionSize(collectionName);
                if (knownSize > 0 && index >= knownSize)
                {
                    notifyIssue(QStringLiteral("El índice %1 está fuera de rango para la colección actual.").arg(index + 1));
                }
            }
            if (index < 0)
            {
                addCodeLine(QStringLiteral("if (!%1.empty()) { %1.pop_back(); }").arg(collectionName));
            }
            else
            {
                addCodeLine(QStringLiteral("if (%1.size() > %2) { %1.erase(%1.begin() + %2); }").arg(collectionName).arg(index));
            }
            auto itRemove = m_collections.find(collectionName);
            if (itRemove != m_collections.end())
            {
                if (itRemove->size > 0)
                {
                    itRemove->size -= 1;
                }
            }
            return true;
        }

        bool handleSortCollection(const QString &original, const QString &normalized)
        {
            Q_UNUSED(original);
            QRegularExpression re(QStringLiteral("^ordenar (?:la|el) (lista|vector|arreglo)(?: de forma (ascendente|descendente))?$"));
            QRegularExpressionMatch match = re.match(normalized);
            if (!match.hasMatch())
            {
                return false;
            }

            QString alias = match.captured(1);
            QString order = match.captured(2);

            QString collectionName = collectionNameForAlias(alias);
            if (collectionName.isEmpty())
            {
                collectionName = lastCollection();
            }
            if (collectionName.isEmpty())
            {
                notifyIssue(QStringLiteral("No se encontró ninguna colección disponible para esta instrucción."));
                return false;
            }

            const auto info = m_collections.value(collectionName);
            QString elementType = info.elementType;
            if (elementType.isEmpty())
            {
                notifyIssue(QStringLiteral("No se pudo determinar el tipo de datos de la colección."));
                return false;
            }
            ensureInclude("algorithm");
            if (info.isCArray)
            {
                if (info.size <= 0)
                {
                    notifyIssue(QStringLiteral("No se conoce el tamaño del arreglo para ordenarlo."));
                    return true;
                }
                if (order == QStringLiteral("descendente"))
                {
                    addCodeLine(QStringLiteral("std::sort(%1, %1 + %2, [](const %3 &a, const %3 &b){ return a > b; });")
                                    .arg(collectionName)
                                    .arg(info.size)
                                    .arg(elementType));
                }
                else
                {
                    addCodeLine(QStringLiteral("std::sort(%1, %1 + %2);").arg(collectionName).arg(info.size));
                }
            }
            else
            {
                ensureInclude("vector");
                if (order == QStringLiteral("descendente"))
                {
                    addCodeLine(QStringLiteral("std::sort(%1.begin(), %1.end(), [](const %2 &a, const %2 &b){ return a > b; });")
                                    .arg(collectionName)
                                    .arg(elementType));
                }
                else
                {
                    addCodeLine(QStringLiteral("std::sort(%1.begin(), %1.end());").arg(collectionName));
                }
            }
            return true;
        }

        bool handleIterateCollection(const QString &original, const QString &normalized)
        {
            Q_UNUSED(original);
            if (!normalized.startsWith(QStringLiteral("recorrer la")) &&
                !normalized.startsWith(QStringLiteral("recorrer el")))
            {
                return false;
            }

            QRegularExpression simpleRe("^recorrer (?:la|el) (lista|vector|arreglo)$");
            QRegularExpressionMatch simpleMatch = simpleRe.match(normalized);
            if (simpleMatch.hasMatch())
            {
                QString alias = simpleMatch.captured(1);
                QString collectionName = collectionNameForAlias(alias);
                if (collectionName.isEmpty())
                {
                    collectionName = lastCollection();
                }
                if (collectionName.isEmpty())
                {
                    notifyIssue(QStringLiteral("No se encontró la colección a recorrer."));
                    return true;
                }

                CollectionInfo info = m_collections.value(collectionName);
                QString indexName = QStringLiteral("i");
                if (hasVariable(indexName))
                {
                    indexName = QStringLiteral("i%1").arg(m_tempCounter++);
                }

                if (info.isCArray)
                {
                    startBlock(QStringLiteral("for (std::size_t %1 = 0; %1 < %2; ++%1) {").arg(indexName).arg(info.size),
                               BlockType::Loop, true, m_currentIndent);
                }
                else
                {
                    startBlock(QStringLiteral("for (std::size_t %1 = 0; %1 < %2.size(); ++%1) {").arg(indexName, collectionName),
                               BlockType::Loop, true, m_currentIndent);
                }

                return true;
            }

            return handleIterateCollectionSum(original, normalized);
        }

        bool handleIterateCollectionSum(const QString &original, const QString &normalized)
        {
            Q_UNUSED(original);
            if (!normalized.startsWith(QStringLiteral("recorrer la")))
            {
                return false;
            }

            QRegularExpression re("^recorrer la (lista|vector|arreglo) y sumar cada elemento (?:al|en) ([a-zA-Z_][a-zA-Z0-9_]*)$");
            QRegularExpressionMatch match = re.match(normalized);
            if (!match.hasMatch())
            {
                return false;
            }

            QString alias = match.captured(1);
            QString destination = sanitizedIdentifier(match.captured(2));

            QString collectionName = collectionNameForAlias(alias);
            if (collectionName.isEmpty())
            {
                collectionName = lastCollection();
            }
            if (collectionName.isEmpty())
            {
                notifyIssue(QStringLiteral("No se encontró ninguna colección disponible para esta instrucción."));
                return false;
            }

            QString elementType = elementTypeForCollection(collectionName);
            if (elementType.isEmpty())
            {
                notifyIssue(QStringLiteral("No se pudo determinar el tipo de datos de la colección."));
                return false;
            }

            QString itemName = QStringLiteral("item");
            if (hasVariable(itemName))
            {
                itemName = QStringLiteral("item%1").arg(m_tempCounter++);
            }

            QString sumType = QStringLiteral("int");
            QString sumDefault = QStringLiteral("0");
            if (elementType == QStringLiteral("double"))
            {
                sumType = QStringLiteral("double");
                sumDefault = QStringLiteral("0.0");
            }
            else if (elementType == QStringLiteral("float"))
            {
                sumType = QStringLiteral("float");
                sumDefault = QStringLiteral("0.0f");
            }

            ensureVariable(destination, sumType, sumDefault);

            addCodeLine(QStringLiteral("for (const %1 &%2 : %3) {").arg(elementType, itemName, collectionName));
            ++m_indentLevel;
            addCodeLine(QStringLiteral("%1 += %2;").arg(destination, itemName));
            --m_indentLevel;
            addCodeLine(QStringLiteral("}"));
            return true;
        }

        bool handleIfCondition(const QString &original, const QString &normalized)
        {
            if (!normalized.startsWith(QStringLiteral("si ")))
            {
                return false;
            }

            QString rest = normalized.mid(3);
            QString condition;
            QString action;

            int mostrarIdx = rest.indexOf(QStringLiteral(" mostrar "));
            int imprimirIdx = rest.indexOf(QStringLiteral(" imprimir "));
            int idx = mostrarIdx >= 0 ? mostrarIdx : imprimirIdx;
            QString actionVerb = mostrarIdx >= 0 ? QStringLiteral("mostrar") : QStringLiteral("imprimir");

            if (idx >= 0)
            {
                condition = rest.left(idx).trimmed();
                action = rest.mid(idx + actionVerb.size() + 1).trimmed();
            }
            else
            {
                condition = rest.trimmed();
            }

            QString conditionExpr = translateCondition(condition);
            if (conditionExpr.isEmpty())
            {
                notifyIssue(QStringLiteral("No se pudo interpretar la condición del 'si': %1").arg(condition));
                return false;
            }

            startBlock(QStringLiteral("if (%1) {").arg(conditionExpr), BlockType::If, true, m_currentIndent);

            if (!action.isEmpty())
            {
                QString message = readQuotedText(original);
                if (message.isEmpty())
                {
                    message = action;
                }
                ensureInclude("iostream");
                addCodeLine(QStringLiteral("std::cout << %1 << std::endl;").arg(quoted(message)));
            }

            return true;
        }

        QString translateCondition(const QString &condition)
        {
            QString norm = condition.trimmed();
            QString normalized = normalizeLine(norm);
            normalized.replace(QStringLiteral(" es "), QStringLiteral(" "));

            struct
            {
                QString keyword;
                QString op;
            } ops[] = {
                {QStringLiteral("mayor que"), QStringLiteral(">")},
                {QStringLiteral("menor que"), QStringLiteral("<")},
                {QStringLiteral("mayor o igual que"), QStringLiteral(">=")},
                {QStringLiteral("menor o igual que"), QStringLiteral("<=")},
                {QStringLiteral("igual a"), QStringLiteral("==")},
                {QStringLiteral("diferente de"), QStringLiteral("!=")}};

            for (const auto &entry : ops)
            {
                int idx = normalized.indexOf(entry.keyword);
                if (idx >= 0)
                {
                    QString left = normalized.left(idx).trimmed();
                    QString right = normalized.mid(idx + entry.keyword.size()).trimmed();

                    QString leftExpr = translateExpressionPart(left);
                    QString rightExpr = translateExpressionPart(right);

                    if (!leftExpr.isEmpty() && !rightExpr.isEmpty())
                    {
                        if (entry.op == QStringLiteral("==") && rightExpr == QStringLiteral("false"))
                        {
                            return QStringLiteral("!%1").arg(leftExpr);
                        }
                        return QStringLiteral("%1 %2 %3").arg(leftExpr, entry.op, rightExpr);
                    }
                }
            }

            return QString();
        }

        QString translateExpressionPart(const QString &part)
        {
            QString trimmed = part.trimmed();

            QRegularExpression arrayAccess(QStringLiteral("^([a-zA-Z_][a-zA-Z0-9_]*)\\[([a-zA-Z_][a-zA-Z0-9_]*)\\]$"));
            QRegularExpressionMatch arrayMatch = arrayAccess.match(trimmed);
            if (arrayMatch.hasMatch())
            {
                QString arrayName = sanitizedIdentifier(arrayMatch.captured(1));
                QString indexName = sanitizedIdentifier(arrayMatch.captured(2));

                if (!hasCollection(arrayName))
                {
                    arrayName = collectionNameForAlias(arrayMatch.captured(1));
                }

                if (hasCollection(arrayName))
                {
                    if (hasVariable(indexName))
                    {
                        return QStringLiteral("%1[%2]").arg(arrayName, indexName);
                    }
                    else if (hasVariable(QStringLiteral("idx")))
                    {
                        return QStringLiteral("%1[idx]").arg(arrayName);
                    }
                    else if (hasVariable(QStringLiteral("i")))
                    {
                        return QStringLiteral("%1[i]").arg(arrayName);
                    }
                    else
                    {
                        return QStringLiteral("%1[%2]").arg(arrayName, indexName);
                    }
                }
            }

            QRegularExpression numberRegex(QStringLiteral("^-?\\d+(?:[\\.,]\\d+)?$"));
            if (numberRegex.match(trimmed).hasMatch())
            {
                return ensureNumberString(trimmed, trimmed.contains('.') || trimmed.contains(','));
            }

            if (trimmed == QStringLiteral("verdadero") || trimmed == QStringLiteral("true"))
            {
                return QStringLiteral("true");
            }
            if (trimmed == QStringLiteral("falso") || trimmed == QStringLiteral("false"))
            {
                return QStringLiteral("false");
            }

            QString identifier = sanitizedIdentifier(trimmed);
            if (hasVariable(identifier))
            {
                return identifier;
            }

            if (trimmed.startsWith(QStringLiteral("el ")) || trimmed.startsWith(QStringLiteral("la ")))
            {
                QString withoutArticle = trimmed.mid(3).trimmed();
                QString articleIdentifier = sanitizedIdentifier(withoutArticle);
                if (hasVariable(articleIdentifier))
                {
                    return articleIdentifier;
                }
                if (withoutArticle == QStringLiteral("numero"))
                {
                    if (!hasVariable(QStringLiteral("numero")))
                    {
                        ensureVariable(QStringLiteral("numero"), QStringLiteral("int"), QStringLiteral("0"));
                    }
                    return QStringLiteral("numero");
                }
            }

            return sanitizedIdentifier(trimmed);
        }

        bool handleElse(const QString &original, const QString &normalized)
        {
            if (m_blocks.isEmpty() || m_blocks.last().type != BlockType::If)
            {
                notifyIssue(QStringLiteral("Se encontró un 'sino' sin un 'si' previo."));
                return false;
            }

            if (normalized.startsWith(QStringLiteral("sino si ")))
            {
                if (m_blocks.last().hasElse)
                {
                    notifyIssue(QStringLiteral("No se puede usar 'sino si' después de un 'sino' final."));
                    return false;
                }

                if (m_indentLevel > 1)
                {
                    --m_indentLevel;
                }

                QString condition = normalized.mid(QStringLiteral("sino si ").size()).trimmed();
                QString conditionExpr = translateCondition(condition);
                if (conditionExpr.isEmpty())
                {
                    notifyIssue(QStringLiteral("No se pudo interpretar la condición del 'sino si': %1").arg(condition));
                    return true;
                }

                m_codeLines.append(indent() + QStringLiteral("} else if (%1) {").arg(conditionExpr));

                ++m_indentLevel;
                m_blocks.last().hasElseIf = true;
                m_blocks.last().autoClose = true;
                m_blocks.last().indent = m_currentIndent;
                return true;
            }

            if (m_blocks.last().hasElse)
            {
                notifyIssue(QStringLiteral("El bloque 'si' ya tenía un 'sino' asociado."));
                return false;
            }

            if (m_indentLevel > 1)
            {
                --m_indentLevel;
            }
            m_codeLines.append(indent() + QStringLiteral("} else {"));
            ++m_indentLevel;
            m_blocks.last().hasElse = true;
            m_blocks.last().autoClose = true;
            m_blocks.last().indent = m_currentIndent;

            if (normalized.startsWith(QStringLiteral("sino mostrar")) || normalized.startsWith(QStringLiteral("sino imprimir")))
            {
                QString message = readQuotedText(original);
                if (!message.isEmpty())
                {
                    ensureInclude("iostream");
                    addCodeLine(QStringLiteral("std::cout << %1 << std::endl;").arg(quoted(message)));
                }
            }

            return true;
        }

        bool handleShowMessage(const QString &original, const QString &normalized)
        {
            if (!normalized.startsWith(QStringLiteral("mostrar")) &&
                !normalized.startsWith(QStringLiteral("imprimir")))
            {
                return false;
            }

            QString message = readQuotedText(original);
            QString appendedVar;

            int firstQuote = original.indexOf('"');
            int secondQuote = (firstQuote >= 0) ? original.indexOf('"', firstQuote + 1) : -1;
            if (secondQuote >= 0)
            {
                QString tail = original.mid(secondQuote + 1).trimmed();
                if (tail.startsWith(QStringLiteral("y ")))
                {
                    appendedVar = tail.mid(2).trimmed();
                    if (appendedVar == QStringLiteral("i") || appendedVar == QStringLiteral("idx"))
                    {
                    }
                    else
                    {
                        appendedVar = sanitizedIdentifier(appendedVar);
                    }
                }
            }

            if (message.isEmpty())
            {
                QString rest = normalized;
                int idx = rest.indexOf(' ');
                if (idx >= 0)
                {
                    message = rest.mid(idx + 1).trimmed();
                }
                else
                {
                    message = rest;
                }
            }

            QStringList parts;
            parts << quoted(message);

            if (!appendedVar.isEmpty())
            {
                parts << appendedVar;
            }

            ensureInclude("iostream");
            addCodeLine(QStringLiteral("std::cout << %1 << std::endl;").arg(parts.join(QStringLiteral(" << "))));
            return true;
        }

        bool requestInputForCollection(const QString &alias)
        {
            QString collection = alias.isEmpty() ? lastCollection() : collectionNameForAlias(alias);
            if (collection.isEmpty())
            {
                collection = lastCollection();
            }
            if (collection.isEmpty())
            {
                notifyIssue(QStringLiteral("No se encontró ninguna colección disponible para ingresar datos."));
                return true;
            }

            if (!hasCollection(collection))
            {
                notifyIssue(QStringLiteral("No se encontró ninguna colección disponible para ingresar datos."));
                return true;
            }

            CollectionInfo info = m_collections.value(collection);
            ensureInclude("iostream");

            QString indexName = QStringLiteral("i");
            if (hasVariable(indexName))
            {
                indexName = QStringLiteral("i%1").arg(m_tempCounter++);
            }

            QString promptLine;
            if (info.elementType == QStringLiteral("double") && collection.contains(QStringLiteral("nota")))
            {
                promptLine = QStringLiteral(R"(std::cout << "Ingrese la nota " << (%1 + 1) << ": ";)");
            }
            else
            {
                promptLine = QStringLiteral(R"(std::cout << "Ingrese el valor " << (%1 + 1) << ": ";)");
            }

            if (info.isCArray)
            {
                if (info.size <= 0)
                {
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

        bool handleUserInput(const QString &original, const QString &normalized)
        {
            Q_UNUSED(original);

            bool matched = false;
            QString collectionAlias;

            if (normalized.startsWith(QStringLiteral("pedir al usuario")))
            {
                matched = true;
            }
            else if (normalized.startsWith(QStringLiteral("ingresar valor de cada ")))
            {
                QRegularExpression re(QStringLiteral("^ingresar valor de cada (.+) en (?:la|el) (lista|vector|arreglo)$"));
                QRegularExpressionMatch match = re.match(normalized);
                if (match.hasMatch())
                {
                    collectionAlias = match.captured(2);
                    matched = true;
                }
            }
            else if (normalized.startsWith(QStringLiteral("ingresar valor de cada")) && normalized.contains(QStringLiteral("lista")))
            {
                matched = true;
                collectionAlias = QStringLiteral("lista");
            }

            if (!matched)
            {
                return false;
            }

            return requestInputForCollection(collectionAlias);
        }

        bool handleRequestNumberInput(const QString &original, const QString &normalized)
        {
            Q_UNUSED(original);

            if ((normalized.contains(QStringLiteral("pedir al usuario")) && normalized.contains(QStringLiteral("ingrese")) && normalized.contains(QStringLiteral("numero"))) ||
                (normalized.contains(QStringLiteral("solicitar")) && normalized.contains(QStringLiteral("usuario")) && normalized.contains(QStringLiteral("numero"))) ||
                (normalized.contains(QStringLiteral("pedir")) && normalized.contains(QStringLiteral("ingrese")) && normalized.contains(QStringLiteral("consola"))))
            {

                return requestInputForCollection(QStringLiteral(""));
            }

            return false;
        }

        bool handlePrintCollection(const QString &original, const QString &normalized)
        {
            Q_UNUSED(original);

            bool isPrintCollectionInstruction = false;
            if (normalized.startsWith(QStringLiteral("imprimir todos los elementos del")) ||
                normalized.startsWith(QStringLiteral("mostrar todos los elementos del")) ||
                normalized.startsWith(QStringLiteral("imprimir todos los elementos de")) ||
                normalized.startsWith(QStringLiteral("mostrar todos los elementos de")) ||
                normalized.startsWith(QStringLiteral("imprimir todos los elementos de la")) ||
                normalized.startsWith(QStringLiteral("mostrar todos los elementos de la")) ||
                (normalized.contains(QStringLiteral("imprimir")) && normalized.contains(QStringLiteral("todos los elementos")) && (normalized.contains(QStringLiteral("vector")) || normalized.contains(QStringLiteral("lista")) || normalized.contains(QStringLiteral("arreglo")))) ||
                (normalized.contains(QStringLiteral("mostrar")) && normalized.contains(QStringLiteral("todos los elementos")) && (normalized.contains(QStringLiteral("vector")) || normalized.contains(QStringLiteral("lista")) || normalized.contains(QStringLiteral("arreglo")))))
            {
                isPrintCollectionInstruction = true;
            }

            if (!isPrintCollectionInstruction)
            {
                return false;
            }

            QString collection = lastCollection();
            if (collection.isEmpty())
            {
                return false;
            }

            CollectionInfo info = m_collections.value(collection);
            ensureInclude("iostream");

            QString indexName = QStringLiteral("valor");
            if (hasVariable(indexName))
            {
                indexName = QStringLiteral("valor%1").arg(m_tempCounter++);
            }
            if (info.isCArray)
            {
                if (info.size <= 0)
                {
                    notifyIssue(QStringLiteral("No se conoce el tamaño del arreglo para imprimir sus elementos."));
                    return true;
                }
                addCodeLine(QStringLiteral("for (int %1 = 0; %1 < %2; ++%1) {").arg(indexName).arg(info.size));
                ++m_indentLevel;
                addCodeLine(QStringLiteral("std::cout << %1[%2] << std::endl;").arg(collection).arg(indexName));
                --m_indentLevel;
                addCodeLine(QStringLiteral("}"));
            }
            else
            {
                ensureInclude("vector");
                addCodeLine(QStringLiteral("for (const %1 &%2 : %3) {").arg(info.elementType, indexName, collection));
                ++m_indentLevel;
                addCodeLine(QStringLiteral("std::cout << %1 << std::endl;").arg(indexName));
                --m_indentLevel;
                addCodeLine(QStringLiteral("}"));
            }
            return true;
        }

        bool handleReadDataFile(const QString &original, const QString &normalized)
        {
            Q_UNUSED(original);

            bool isDataReadInstruction = false;
            if (normalized.startsWith(QStringLiteral("leer los datos")) ||
                normalized.startsWith(QStringLiteral("cargar los datos")) ||
                normalized.startsWith(QStringLiteral("importar los datos")) ||
                normalized.startsWith(QStringLiteral("leer desde")) ||
                normalized.startsWith(QStringLiteral("cargar desde")) ||
                normalized.startsWith(QStringLiteral("importar desde")) ||
                normalized.startsWith(QStringLiteral("leer los datos desde")) ||
                normalized.startsWith(QStringLiteral("cargar los datos desde")) ||
                normalized.startsWith(QStringLiteral("importar los datos desde")) ||
                (normalized.contains(QStringLiteral("leer")) && normalized.contains(QStringLiteral("archivo"))) ||
                (normalized.contains(QStringLiteral("cargar")) && normalized.contains(QStringLiteral("archivo"))) ||
                (normalized.contains(QStringLiteral("importar")) && normalized.contains(QStringLiteral("archivo"))))
            {
                isDataReadInstruction = true;
            }

            if (!isDataReadInstruction)
            {
                return false;
            }

            if (m_input.dataFileContents.trimmed().isEmpty())
            {
                notifyIssue(QStringLiteral("Error: No se ha cargado ningún archivo de datos. Use el botón 'Cargar Datos' para cargar un archivo .txt antes de usar instrucciones de lectura de datos."));
                m_success = false;
                return true;
            }

            QRegularExpression re("archivo llamado ([^\\s,\\.]+(?:\\.[^\\s,]+)?)");
            QRegularExpressionMatch match = re.match(normalized);
            QString fileName = match.hasMatch() ? match.captured(1).trimmed() : m_dataFileName;

            if (!match.hasMatch())
            {
                QRegularExpression re2("desde archivo ([^\\s,\\.]+(?:\\.[^\\s,]+)?)");
                QRegularExpressionMatch match2 = re2.match(normalized);
                if (match2.hasMatch())
                {
                    fileName = match2.captured(1).trimmed();
                }
            }
            if (fileName.isEmpty())
            {
                fileName = m_dataFileName;
            }

            QStringList lines = m_input.dataFileContents.split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts);
            if (lines.isEmpty())
            {
                notifyIssue(QStringLiteral("Error: El archivo de datos está vacío."));
                return false;
            }

            QString firstLine = lines.first().trimmed();
            QStringList sampleParts = firstLine.split(',');
            int columnCount = sampleParts.size();

            if (columnCount == 1)
            {
                return handleSingleColumnData(lines);
            }

            return handleMultiColumnData(lines, columnCount);
        }

        bool handleSingleColumnData(const QStringList &lines)
        {
            QString targetCollection = lastCollection();
            if (targetCollection.isEmpty())
            {
                notifyIssue(QStringLiteral("Error: No se encontró ninguna lista para cargar los datos. Cree una lista antes de leer los datos."));
                return false;
            }

            CollectionInfo collInfo = m_collections.value(targetCollection);

            ensureInclude("vector");
            if (collInfo.elementType == QStringLiteral("std::string"))
            {
                ensureInclude("string");
            }
            ensureInclude("iostream");

            addCodeLine(QStringLiteral("// Cargar datos desde archivo (una columna)"));

            for (const QString &line : lines)
            {
                QString trimmedLine = line.trimmed();
                if (trimmedLine.isEmpty())
                    continue;

                if (collInfo.elementType == QStringLiteral("std::string") ||
                    !isValidNumber(trimmedLine))
                {
                    addCodeLine(QStringLiteral("%1.push_back(%2);").arg(targetCollection, quoted(trimmedLine)));
                }
                else
                {
                    QString numberValue = ensureNumberString(trimmedLine,
                                                             collInfo.elementType == QStringLiteral("double") || trimmedLine.contains('.') || trimmedLine.contains(','));
                    addCodeLine(QStringLiteral("%1.push_back(%2);").arg(targetCollection, numberValue));
                }
            }

            auto collIt = m_collections.find(targetCollection);
            if (collIt != m_collections.end())
            {
                collIt.value().size = lines.size();
            }

            return true;
        }

        bool handleMultiColumnData(const QStringList &lines, int columnCount)
        {
            QStringList recentCollections = getLastNCollections(columnCount);

            if (recentCollections.size() < columnCount)
            {
                notifyIssue(QStringLiteral("Error: Se necesitan %1 listas para los datos de %1 columnas, pero solo se encontraron %2. Cree más listas antes de leer los datos.")
                                .arg(columnCount)
                                .arg(recentCollections.size()));
                return false;
            }

            ensureInclude("vector");
            ensureInclude("string");
            ensureInclude("iostream");

            addCodeLine(QStringLiteral("// Cargar datos desde archivo (%1 columnas)").arg(columnCount));

            int processedLines = 0;
            for (const QString &line : lines)
            {
                QString trimmedLine = line.trimmed();
                if (trimmedLine.isEmpty())
                    continue;

                QStringList parts = trimmedLine.split(',');
                if (parts.size() < columnCount)
                {
                    continue;
                }

                for (int i = 0; i < columnCount && i < recentCollections.size(); i++)
                {
                    QString collectionName = recentCollections[i];
                    QString value = parts[i].trimmed();

                    if (value.startsWith('"') && value.endsWith('"'))
                    {
                        value = value.mid(1, value.length() - 2);
                    }

                    CollectionInfo collInfo = m_collections.value(collectionName);

                    if (collInfo.elementType == QStringLiteral("std::string") || !isValidNumber(value))
                    {
                        addCodeLine(QStringLiteral("%1.push_back(%2);").arg(collectionName, quoted(value)));
                    }
                    else
                    {
                        QString numberValue = ensureNumberString(value,
                                                                 collInfo.elementType == QStringLiteral("double") || value.contains('.') || value.contains(','));
                        addCodeLine(QStringLiteral("%1.push_back(%2);").arg(collectionName, numberValue));
                    }
                }
                processedLines++;
            }

            for (const QString &collectionName : recentCollections)
            {
                auto collIt = m_collections.find(collectionName);
                if (collIt != m_collections.end())
                {
                    collIt.value().size = processedLines;
                }
            }

            return true;
        }

        QStringList getLastNCollections(int n)
        {
            QStringList collections;

            int start = qMax(0, m_collectionOrder.size() - n);
            for (int i = start; i < m_collectionOrder.size(); i++)
            {
                collections.append(m_collectionOrder[i]);
            }

            return collections;
        }

        bool isValidNumber(const QString &text)
        {
            bool ok;
            text.toDouble(&ok);
            return ok;
        }

        bool handlePrintPairs(const QString &original, const QString &normalized)
        {
            Q_UNUSED(original);

            bool isPrintPairsInstruction = false;
            if (normalized.startsWith(QStringLiteral("imprimir los paises")) ||
                normalized.startsWith(QStringLiteral("mostrar los paises")) ||
                (normalized.contains(QStringLiteral("imprimir")) && normalized.contains(QStringLiteral("paises")) && normalized.contains(QStringLiteral("capitales"))) ||
                (normalized.contains(QStringLiteral("mostrar")) && normalized.contains(QStringLiteral("paises")) && normalized.contains(QStringLiteral("capitales"))))
            {
                isPrintPairsInstruction = true;
            }

            if (!isPrintPairsInstruction)
            {
                return false;
            }

            if (m_input.dataFileContents.trimmed().isEmpty())
            {
                notifyIssue(QStringLiteral("Error: Esta instrucción requiere datos cargados de un archivo. Use el botón 'Cargar Datos' para cargar un archivo .txt con el formato 'País,Capital' antes de proceder."));
                m_success = false;
                return true;
            }

            QString paises = collectionNameForAlias(QStringLiteral("paises"));
            QString capitales = collectionNameForAlias(QStringLiteral("capitales"));
            if (paises.isEmpty() || capitales.isEmpty())
            {
                notifyIssue(QStringLiteral("Error: No se encontraron las listas de países y capitales. Asegúrese de crear las listas antes de imprimir."));
                return false;
            }

            ensureInclude("iostream");
            ensureInclude("vector");
            ensureInclude("string");

            QString indexName = QStringLiteral("i");
            if (hasVariable(indexName))
            {
                indexName = QStringLiteral("i%1").arg(m_tempCounter++);
            }

            addCodeLine(QStringLiteral("for (std::size_t %1 = 0; %1 < %2.size() && %1 < %3.size(); ++%1) {").arg(indexName, paises, capitales));
            ++m_indentLevel;
            addCodeLine(QStringLiteral("std::cout << %1[%2] << \" - \" << %3[%2] << std::endl;").arg(paises, indexName, capitales));
            --m_indentLevel;
            addCodeLine(QStringLiteral("}"));
            return true;
        }

        bool handleCreateStruct(const QString &original, const QString &normalized)
        {
            Q_UNUSED(original);
            if (!normalized.startsWith(QStringLiteral("crear estructura")))
            {
                return false;
            }

            QString rest = normalized.mid(QStringLiteral("crear estructura").size()).trimmed();

            QRegularExpression structRe(QStringLiteral("^([a-zA-Z_][a-zA-Z0-9_]*) con (.+)$"));
            QRegularExpressionMatch match = structRe.match(rest);
            if (!match.hasMatch())
            {
                notifyIssue(QStringLiteral("Formato de estructura no reconocido: %1").arg(normalized));
                return false;
            }

            QString structName = match.captured(1).trimmed();
            QString fieldsText = match.captured(2).trimmed();

            QRegularExpression fieldRe(QStringLiteral("([a-zA-Z_][a-zA-Z0-9_]*) \\(([^)]+)\\)"));
            QRegularExpressionMatchIterator fieldIt = fieldRe.globalMatch(fieldsText);

            StructInfo structInfo;
            structInfo.name = structName;

            while (fieldIt.hasNext())
            {
                QRegularExpressionMatch fieldMatch = fieldIt.next();
                QString fieldName = fieldMatch.captured(1).trimmed();
                QString fieldTypeText = fieldMatch.captured(2).trimmed();

                QString fieldType;
                if (fieldTypeText.contains(QStringLiteral("cadena de texto")) || fieldTypeText.contains(QStringLiteral("texto")))
                {
                    fieldType = QStringLiteral("std::string");
                    ensureInclude("string");
                }
                else if (fieldTypeText.contains(QStringLiteral("entero")))
                {
                    fieldType = QStringLiteral("int");
                }
                else if (fieldTypeText.contains(QStringLiteral("decimal")))
                {
                    fieldType = QStringLiteral("double");
                }
                else
                {
                    fieldType = QStringLiteral("int");
                }

                structInfo.fieldNames << fieldName;
                structInfo.fieldTypes << fieldType;
            }

            if (structInfo.fieldNames.isEmpty())
            {
                notifyIssue(QStringLiteral("No se encontraron campos válidos en la estructura: %1").arg(normalized));
                return false;
            }

            m_structs.insert(structName, structInfo);
            return true;
        }

        bool handleCreateStructCollection(const QString &original, const QString &normalized)
        {
            Q_UNUSED(original);
            if (!normalized.startsWith(QStringLiteral("crear lista de")))
            {
                return false;
            }

            QRegularExpression structCollRe(QStringLiteral("^crear lista de ([a-zA-Z_][a-zA-Z0-9_]*) con (\\d+) elementos?$"));
            QRegularExpressionMatch match = structCollRe.match(normalized);
            if (!match.hasMatch())
            {
                return false;
            }

            QString structType = match.captured(1).trimmed();
            QString sizeStr = match.captured(2).trimmed();

            if (!m_structs.contains(structType))
            {
                notifyIssue(QStringLiteral("Estructura no definida: %1").arg(structType));
                return false;
            }

            QString collectionName = QStringLiteral("lista");
            QString uniqueCollectionName = getUniqueVariableName(collectionName);

            ensureInclude("vector");

            CollectionInfo collInfo;
            collInfo.type = QStringLiteral("std::vector<%1>").arg(structType);
            collInfo.elementType = structType;
            collInfo.alias = QStringLiteral("lista");
            collInfo.size = sizeStr.toInt();
            collInfo.fixedSize = true;
            collInfo.isCArray = false;

            addCodeLine(QStringLiteral("std::vector<%1> %2(%3);").arg(structType, uniqueCollectionName, sizeStr));
            registerCollection(uniqueCollectionName, collInfo);

            return true;
        }

        bool handleInputStructData(const QString &original, const QString &normalized)
        {
            Q_UNUSED(original);
            if (!normalized.startsWith(QStringLiteral("ingresar los datos de cada")))
            {
                return false;
            }

            QRegularExpression inputRe(QStringLiteral("^ingresar los datos de cada ([a-zA-Z_][a-zA-Z0-9_]*)$"));
            QRegularExpressionMatch match = inputRe.match(normalized);
            if (!match.hasMatch())
            {
                return false;
            }

            QString structTypeStr = match.captured(1).trimmed();

            StructInfo structInfo;
            bool found = false;
            for (auto it = m_structs.constBegin(); it != m_structs.constEnd(); ++it)
            {
                if (it.key().toLower() == structTypeStr.toLower())
                {
                    structInfo = it.value();
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                notifyIssue(QStringLiteral("Tipo de estructura no encontrado: %1").arg(structTypeStr));
                return false;
            }

            QString collectionName;
            for (auto it = m_collections.constBegin(); it != m_collections.constEnd(); ++it)
            {
                if (it.value().elementType.toLower() == structInfo.name.toLower())
                {
                    collectionName = it.key();
                    break;
                }
            }

            if (collectionName.isEmpty())
            {
                notifyIssue(QStringLiteral("No se encontró una colección para el tipo: %1").arg(structInfo.name));
                return false;
            }

            ensureInclude("iostream");

            QString indexName = QStringLiteral("i");
            if (hasVariable(indexName))
            {
                indexName = QStringLiteral("i%1").arg(m_tempCounter++);
            }
            addCodeLine(QStringLiteral("for (std::size_t %1 = 0; %1 < %2.size(); ++%1) {").arg(indexName, collectionName));
            ++m_indentLevel;

            for (int i = 0; i < structInfo.fieldNames.size() && i < structInfo.fieldTypes.size(); ++i)
            {
                QString fieldName = structInfo.fieldNames[i];
                QString fieldType = structInfo.fieldTypes[i];

                QString promptMessage;
                if (fieldType == QStringLiteral("std::string"))
                {
                    promptMessage = QStringLiteral("\"Ingrese el %1 del %2 \" << (%3 + 1) << \": \"").arg(fieldName, structTypeStr, indexName);
                }
                else if (fieldType == QStringLiteral("int"))
                {
                    promptMessage = QStringLiteral("\"Ingrese la %1 de \" << %2[%3].nombre << \": \"").arg(fieldName, collectionName, indexName);
                }
                else if (fieldType == QStringLiteral("double"))
                {
                    promptMessage = QStringLiteral("\"Ingrese la %1 de \" << %2[%3].nombre << \": \"").arg(fieldName, collectionName, indexName);
                }

                if (i == 0)
                {
                    promptMessage = QStringLiteral("\"Ingrese el %1 del %2 \" << (%3 + 1) << \": \"").arg(fieldName, structTypeStr, indexName);
                }

                addCodeLine(QStringLiteral("std::cout << %1;").arg(promptMessage));
                addCodeLine(QStringLiteral("std::cin >> %1[%2].%3;").arg(collectionName, indexName, fieldName));

                if (i < structInfo.fieldNames.size() - 1)
                {
                    addCodeLine(QStringLiteral(""));
                }
            }

            --m_indentLevel;
            addCodeLine(QStringLiteral("}"));

            return true;
        }

        bool handleIterateStructCollection(const QString &original, const QString &normalized)
        {
            Q_UNUSED(original);
            if (!normalized.startsWith(QStringLiteral("recorrer la lista y mostrar")))
            {
                return false;
            }

            QRegularExpression iterRe(QStringLiteral("^recorrer la lista y mostrar (.+)$"));
            QRegularExpressionMatch match = iterRe.match(normalized);
            if (!match.hasMatch())
            {
                return false;
            }

            QString fieldsText = match.captured(1).trimmed();
            QStringList requestedFields = fieldsText.split(QStringLiteral(" y "));

            for (QString &field : requestedFields)
            {
                field = field.trimmed();
            }

            QString collectionName;
            StructInfo structInfo;
            bool foundStruct = false;

            for (auto it = m_collections.constBegin(); it != m_collections.constEnd(); ++it)
            {
                const CollectionInfo &collInfo = it.value();
                if (m_structs.contains(collInfo.elementType))
                {
                    collectionName = it.key();
                    structInfo = m_structs.value(collInfo.elementType);
                    foundStruct = true;
                    break;
                }
            }

            if (!foundStruct)
            {
                notifyIssue(QStringLiteral("No se encontró una colección de estructuras"));
                return false;
            }

            ensureInclude("iostream");

            addCodeLine(QStringLiteral("std::cout << \"\\n--- Registro de estudiantes ---\\n\";"));

            QString iteratorName = QStringLiteral("est");
            addCodeLine(QStringLiteral("for (const auto& %1 : %2) {").arg(iteratorName, collectionName));
            ++m_indentLevel;

            QStringList outputParts;

            for (const QString &requestedField : requestedFields)
            {
                bool fieldExists = false;
                for (const QString &structField : structInfo.fieldNames)
                {
                    if (structField.toLower() == requestedField.toLower())
                    {
                        QString displayName = requestedField;
                        displayName[0] = displayName[0].toUpper();

                        outputParts << QStringLiteral("\"%1: \" << %2.%3").arg(displayName, iteratorName, structField);
                        fieldExists = true;
                        break;
                    }
                }

                if (!fieldExists)
                {
                    notifyIssue(QStringLiteral("Campo no encontrado en la estructura: %1").arg(requestedField));
                }
            }

            if (!outputParts.isEmpty())
            {
                QString outputLine = QStringLiteral("std::cout << %1 << std::endl;").arg(outputParts.join(QStringLiteral(" << \" | \" << ")));
                addCodeLine(outputLine);
            }

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
        QStringList m_collectionOrder;
        QMap<QString, FunctionInfo> m_functions;
        QMap<QString, StructInfo> m_structs;
        QString m_lastCollection;
        QStringList m_issues;
        bool m_success = true;
        int m_indentLevel = 1;
        int m_currentIndent = 0;
        int m_tempCounter = 1;
        QString m_dataFileName;
        bool m_insideFunction = false;
        QString m_currentFunctionName;
    };

}

Parser::Output Parser::convert(const Parser::Input &input)
{
    InstructionParser parser(input);
    return parser.run();
}
