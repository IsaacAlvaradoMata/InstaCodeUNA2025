#include "window.h"
#include <QApplication>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QLabel>
#include <QSpacerItem>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPixmap>
#include <QIcon>
#include <QGraphicsDropShadowEffect>
#include <QStyle>
#include <QFileInfo>
#include <QResizeEvent> // <-- Añade esta inclusión
#include <QEvent>

#include "parser.h"

Window::Window(QWidget *parent)
    : QMainWindow(parent), isDarkTheme(false) { // Inicializa el flag
    setupUI();
    buildAlertOverlay();
}

Window::~Window() {}

void Window::setupUI() {
    // --- Layout del Título con imágenes ---
    QHBoxLayout *titleLayout = new QHBoxLayout();

    // Imagen izquierda
    QLabel *leftImageLabel = new QLabel;
    QPixmap appLogoPixmap(":/images/AppLogo.png");
    leftImageLabel->setPixmap(appLogoPixmap.scaled(40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    leftImageLabel->setAlignment(Qt::AlignRight);

    // Texto del título
     QLabel *titleLabel = new QLabel("<font color='black'>InstaCode</font><font color='red'>UNA</font>");
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-size: 24px; font-weight: bold; padding: 10px;");

    // Imagen derecha
    QLabel *rightImageLabel = new QLabel;
    rightImageLabel->setPixmap(appLogoPixmap.scaled(40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    rightImageLabel->setAlignment(Qt::AlignLeft);

    // Añadir widgets al layout del título
    titleLayout->addStretch();
    titleLayout->addWidget(leftImageLabel);
    titleLayout->addWidget(titleLabel);
    titleLayout->addWidget(rightImageLabel);
    titleLayout->addStretch();

    // --- Widgets de texto ---
    inputTextEdit = new QTextEdit;
    outputTextEdit = new QTextEdit;
    inputTextEdit->setPlaceholderText(" Aquí aparecerá el contenido del archivo .txt");
    outputTextEdit->setPlaceholderText(" Aquí se generará el código C++");

    // --- Estilo texto ---
    // QString editStyle = "padding: 10px; font-size: 14px;";
    // inputTextEdit->setStyleSheet(editStyle);
    // outputTextEdit->setStyleSheet(editStyle);

    // --- Botones ---
    loadButton = new QPushButton(" Cargar archivo .txt"); // Texto actualizado
    dataButton = new QPushButton(" Cargar datos .txt");
    convertButton = new QPushButton(" Convertir a C++");   // Texto actualizado
    exportButton = new QPushButton(" Exportar archivo .cpp"); // Texto actualizado
    themeButton = new QPushButton(" Cambiar Tema");

    // Cargar iconos desde los recursos
    QIcon loadIcon(":/images/folder.png");
    QIcon convertIcon(":/images/settings.png");
    QIcon exportIcon(":/images/downloads.png");
    QIcon themeIcon(":/images/themeChange.png");

    // Establecer iconos en los botones
    loadButton->setIcon(loadIcon);
    dataButton->setIcon(loadIcon);
    convertButton->setIcon(convertIcon);
    exportButton->setIcon(exportIcon);
    themeButton->setIcon(themeIcon);

    // (Opcional) Ajustar el tamaño del icono si es necesario
    QSize iconSize(17,17);
    loadButton->setIconSize(iconSize);
    dataButton->setIconSize(iconSize);
    convertButton->setIconSize(iconSize);
    exportButton->setIconSize(iconSize);
    themeButton->setIconSize(iconSize);

    loadButton->setObjectName("loadButton");
    dataButton->setObjectName("dataButton");
    convertButton->setObjectName("convertButton");
    exportButton->setObjectName("exportButton");
    themeButton->setObjectName("themeButton");


    // QString buttonStyle = "padding: 8px; font-size: 14px;";
    // loadButton->setStyleSheet(buttonStyle);
    // convertButton->setStyleSheet(buttonStyle);
    // exportButton->setStyleSheet(buttonStyle);

    // --- Layout izquierdo (texto + botones) ---
    QVBoxLayout *leftLayout = new QVBoxLayout;
    leftLayout->addWidget(inputTextEdit);
    leftLayout->addWidget(loadButton);
    leftLayout->addWidget(dataButton);
    leftLayout->addWidget(convertButton);

    // --- Layout derecho (texto + botón) ---
    QVBoxLayout *rightLayout = new QVBoxLayout;
    rightLayout->addWidget(outputTextEdit);
    rightLayout->addWidget(exportButton);
    rightLayout->addWidget(themeButton);

    // --- Layout horizontal central ---
    QHBoxLayout *centerLayout = new QHBoxLayout;
    centerLayout->addLayout(leftLayout);
    centerLayout->addLayout(rightLayout);

    // --- Layout principal ---
    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addLayout(titleLayout);
    mainLayout->addLayout(centerLayout);

    // --- Central widget ---
    QWidget *centralWidget = new QWidget(this);
    centralWidget->setLayout(mainLayout);
    setCentralWidget(centralWidget);

    // --- Configuración de ventana ---
    setWindowTitle("InstaCodeUNA - Convertidor de Lenguaje Natural a C++");
    resize(900, 600);

    // --- Conectar botones ---
    connect(loadButton, &QPushButton::clicked, this, &Window::loadFile);
    connect(dataButton, &QPushButton::clicked, this, &Window::loadDataFile);
    connect(convertButton, &QPushButton::clicked, this, &Window::convertToCpp);
    connect(exportButton, &QPushButton::clicked, this, &Window::exportCppFile);
    connect(themeButton, &QPushButton::clicked, this, &Window::toggleTheme); // <-- Conectar nuevo botón
}

void Window::loadFile() {
    QString fileName = QFileDialog::getOpenFileName(this, "Seleccionar archivo de texto", "", "Text Files (*.txt)");
    if (!fileName.isEmpty()) {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            inputTextEdit->setPlainText(in.readAll());
        } else {
            showAlert(AlertType::Error, "No se pudo abrir el archivo",
          "Verifica permisos o que el archivo no esté siendo usado.");
        }
    }
}

void Window::loadDataFile() {
    QString fileName = QFileDialog::getOpenFileName(this, "Seleccionar archivo de datos", "", "Text Files (*.txt)");
    if (fileName.isEmpty()) {
        return;
    }

    QFile file(fileName);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        dataFileContents = in.readAll();
        dataFilePath = fileName;

        const QString baseName = QFileInfo(fileName).fileName();
        showAlert(AlertType::Success,
                  "Datos cargados",
                  "Se cargó " + baseName + " correctamente.",
                  2500);
    } else {
        dataFileContents.clear();
        dataFilePath.clear();
        showAlert(AlertType::Error,
                  "No se pudo abrir el archivo",
                  "Verifica permisos o que el archivo no esté siendo usado.");
    }
}

void Window::convertToCpp() {
    hideAlert();
    QString input = inputTextEdit->toPlainText();
    if (input.trimmed().isEmpty()) {
        showAlert(AlertType::Warning, "Archivo vacío",
                  "El archivo de entrada no contiene instrucciones.");
        outputTextEdit->clear(); // evita previsualizar código vacío
        return;
    }
    Parser::Input parserInput;
    parserInput.instructions = input;
    parserInput.dataFileContents = dataFileContents;
    parserInput.dataFileName = dataFilePath.isEmpty() ? QString() : QFileInfo(dataFilePath).fileName();

    Parser::Output parserOutput = Parser::convert(parserInput);
    outputTextEdit->setPlainText(parserOutput.code);

    if (!parserOutput.success) {
        showAlert(AlertType::Error,
                  "Conversión incompleta",
                  parserOutput.issues.join("\n"));
    } else if (!parserOutput.issues.isEmpty()) {
        showAlert(AlertType::Warning,
                  "Conversión con observaciones",
                  parserOutput.issues.join("\n"));
    } else {
        showAlert(AlertType::Success,
                  "Conversión completada",
                  "El código C++ se generó correctamente.",
                  2500);
    }
}

void Window::exportCppFile() {
    QString fileName = QFileDialog::getSaveFileName(this, "Guardar archivo como", "codigo_generado.cpp", "C++ Files (*.cpp)");
    if (!fileName.isEmpty()) {
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << outputTextEdit->toPlainText();
            file.close();
            showAlert(AlertType::Success, "Exportación completada",
          "El archivo C++ se guardó correctamente.", 2500);
        } else {
            showAlert(AlertType::Error, "No se pudo guardar",
          "Intenta otra ubicación o revisa permisos.");
        }
    }
}

void Window::toggleTheme() {
    isDarkTheme = !isDarkTheme; // Invierte el estado
    if (isDarkTheme) {
        applyStylesheet(":/dark.qss");
    } else {
        applyStylesheet(":/light.qss");
    }
}

void Window::applyStylesheet(const QString &path) {
    QFile styleFile(path);
    if (styleFile.open(QFile::ReadOnly)) {
        QTextStream stream(&styleFile);
        qApp->setStyleSheet(stream.readAll());
    }
}

void Window::buildAlertOverlay()
{
    // Capa semitransparente que cubre toda la ventana
    m_alertOverlay = new QWidget(this);
    m_alertOverlay->setObjectName("alertOverlay");
    m_alertOverlay->hide();
    m_alertOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false); // bloquea clicks al fondo
    m_alertOverlay->setStyleSheet(
        "#alertOverlay { background: rgba(0,0,0,0.35); }"
    );
    m_alertOverlay->setGeometry(this->rect());
    m_alertOverlay->installEventFilter(this);

    // Card centrado
    m_alertCard = new QWidget(m_alertOverlay);
    m_alertCard->setObjectName("alertCard");
    m_alertCard->setMinimumWidth(420);
    m_alertCard->setStyleSheet(
        // Estilo base (el borde izquierdo y colores se ajustan por tipo)
        "#alertCard { background: palette(base); border-radius: 12px; }\n"
        "#alertTitle { font-size: 18px; font-weight: 600; }\n"
        "#alertMessage { font-size: 14px; }\n"
        "#alertClose { border: none; font-weight: 600; padding: 6px 10px; }"
    );

    // Sombra suave
    auto *shadow = new QGraphicsDropShadowEffect(m_alertCard);
    shadow->setBlurRadius(24);
    shadow->setOffset(0, 8);
    shadow->setColor(QColor(0,0,0,80));
    m_alertCard->setGraphicsEffect(shadow);

    // Contenido del card
    m_alertIcon    = new QLabel(m_alertCard);
    m_alertTitle   = new QLabel("Title", m_alertCard);
    m_alertMessage = new QLabel("Message", m_alertCard);
    m_alertClose   = new QPushButton("Cerrar", m_alertCard);

    m_alertTitle->setObjectName("alertTitle");
    m_alertMessage->setObjectName("alertMessage");
    m_alertClose->setObjectName("alertClose");

    m_alertMessage->setWordWrap(true);
    m_alertMessage->setTextInteractionFlags(Qt::TextSelectableByMouse);

    // Fila superior: icono + título + botón Cerrar
    auto *top = new QHBoxLayout();
    top->setContentsMargins(0,0,0,0);
    top->setSpacing(12);
    top->addWidget(m_alertIcon, 0, Qt::AlignTop);
    top->addWidget(m_alertTitle, 1, Qt::AlignVCenter);
    top->addWidget(m_alertClose, 0, Qt::AlignTop);

    // Layout principal del card
    auto *v = new QVBoxLayout(m_alertCard);
    v->setContentsMargins(20,20,20,20);
    v->setSpacing(12);
    v->addLayout(top);
    v->addWidget(m_alertMessage);

    // Cierre manual
    connect(m_alertClose, &QPushButton::clicked, this, [this] {
        hideAlert();
    });

    // Autocierre opcional
    connect(&m_alertTimer, &QTimer::timeout, this, [this] {
        hideAlert();
    });
}

void Window::applyAlertStyle(AlertType type)
{
    QString leftColor;
    QIcon icon;

    switch (type) {
    case AlertType::Info:
        leftColor = "#2F80ED"; // azul
        icon = style()->standardIcon(QStyle::SP_MessageBoxInformation);
        break;
    case AlertType::Success:
        leftColor = "#2EB67D"; // verde
        icon = style()->standardIcon(QStyle::SP_DialogApplyButton);
        break;
    case AlertType::Warning:
        leftColor = "#F2C94C"; // amarillo
        icon = style()->standardIcon(QStyle::SP_MessageBoxWarning);
        break;
    case AlertType::Error:
        leftColor = "#D14343"; // rojo
        icon = style()->standardIcon(QStyle::SP_MessageBoxCritical);
        break;
    }

    // Aplica icono (32px aprox)
    m_alertIcon->setPixmap(icon.pixmap(32, 32));

    // Borde izquierdo coloreado
    m_alertCard->setStyleSheet(
        "#alertCard { background: palette(base); border-radius: 12px; "
        "border-left: 8px solid " + leftColor + "; }\n"
        "#alertTitle { font-size: 18px; font-weight: 600; }\n"
        "#alertMessage { font-size: 14px; }\n"
        "#alertClose { border: none; font-weight: 600; padding: 6px 10px; }"
    );
}

void Window::showAlert(AlertType type,
                       const QString& title,
                       const QString& message,
                       int autoCloseMs)
{
    if (!m_alertOverlay) buildAlertOverlay();

    applyAlertStyle(type);
    m_alertTitle->setText(title);
    m_alertMessage->setText(message);

    // Ocupa toda la ventana y centra el card
    m_alertOverlay->setGeometry(this->rect());
    m_alertOverlay->show();
    m_alertOverlay->raise();

    // Calcula tamaño preferido y centra
    m_alertCard->adjustSize();
    const QSize cs = m_alertCard->sizeHint();
    const QRect R  = m_alertOverlay->rect();
    const int x = R.center().x() - cs.width()/2;
    const int y = R.center().y() - cs.height()/2;
    m_alertCard->move(qMax(12, x), qMax(12, y));
    m_alertCard->show();
    m_alertCard->raise();

    // Autocierre
    m_alertTimer.stop();
    if (autoCloseMs > 0) {
        m_alertTimer.setSingleShot(true);
        m_alertTimer.start(autoCloseMs);
    }
}

void Window::hideAlert()
{
    m_alertTimer.stop();
    if (m_alertOverlay) {
        m_alertOverlay->hide();
    }
}

bool Window::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_alertOverlay && event->type() == QEvent::MouseButtonPress) {
        hideAlert();
        return true;
    }
    return QMainWindow::eventFilter(obj, event);
}


void Window::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    if (m_alertOverlay && m_alertOverlay->isVisible()) {
        m_alertOverlay->setGeometry(this->rect());
        // Recentrar el card
        m_alertCard->adjustSize();
        const QSize cs = m_alertCard->sizeHint();
        const QRect R  = m_alertOverlay->rect();
        const int x = R.center().x() - cs.width()/2;
        const int y = R.center().y() - cs.height()/2;
        m_alertCard->move(qMax(12, x), qMax(12, y));
    }
}
