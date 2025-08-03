#!/bin/bash

echo "🔎 Verificando entorno IntelliSense para VS Code en Mac..."

# 1. Verificar ubicación del archivo c_cpp_properties.json
if [ ! -f .vscode/c_cpp_properties.json ]; then
  echo "❌ No se encontró .vscode/c_cpp_properties.json en el proyecto actual."
  echo "👉 Solución: Asegúrate de estar abriendo la carpeta del proyecto raíz en VS Code."
  exit 1
else
  echo "✅ Archivo .vscode/c_cpp_properties.json encontrado."
fi

# 2. Verificar si compile_commands.json existe (causa de override)
if find . -name compile_commands.json | grep -q compile_commands.json; then
  echo "⚠️  Se encontró un archivo compile_commands.json."
  echo "👉 Esto puede hacer que VS Code ignore c_cpp_properties.json."
else
  echo "✅ No hay compile_commands.json presente."
fi

# 3. Verificar existencia de headers Qt
check_header() {
  if [ ! -f "$1" ]; then
    echo "❌ Falta: $1"
  else
    echo "✅ Encontrado: $1"
  fi
}

echo "📦 Verificando rutas de Qt..."
check_header "/usr/local/opt/qt/include/QtWidgets/QApplication"
check_header "/usr/local/opt/qt/include/QtCore/QString"
check_header "/usr/local/opt/qt/include/QtGui/QFont"

# 4. Verificar compilador
echo "🛠 Verificando clang++..."
if /usr/bin/clang++ --version >/dev/null 2>&1; then
  echo "✅ clang++ funciona correctamente."
else
  echo "❌ clang++ no funciona o no está instalado correctamente."
fi

# 5. Mostrar resumen del workspace
echo "📁 Ruta actual del workspace: $(pwd)"
echo "📁 Contenido base:"
ls -1 | grep -E '(.vscode|.pro|src|main.cpp)' || echo "⚠️ No se ve estructura típica de un proyecto Qt."

echo "✅ Diagnóstico finalizado."
