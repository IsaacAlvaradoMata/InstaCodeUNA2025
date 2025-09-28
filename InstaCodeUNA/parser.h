#ifndef PARSER_H
#define PARSER_H

#include <QString>
#include <QStringList>

class Parser {
public:
    struct Input {
        QString instructions;
        QString dataFileContents;
        QString dataFileName;
    };

    struct Output {
        QString code;
        QStringList issues;
        bool success = true;
    };

    static Output convert(const Input &input);
};

#endif // PARSER_H
