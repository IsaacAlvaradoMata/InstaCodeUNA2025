#!/bin/bash

# Salir si algo falla
set -e

echo "🔍 Buscando qmake..."
QMAKE_PATH="$(brew --prefix qt)/bin/qmake"

# Verificar que qmake exista
if [ ! -f "$QMAKE_PATH" ]; then
  echo "❌ No se encontró qmake en Homebrew. ¿Seguro que Qt está instalado?"
  exit 1
fi

echo "✅ Usando qmake en: $QMAKE_PATH"

# Generar Makefile
echo "📄 Generando Makefile desde InstaCodeUNA.pro..."
"$QMAKE_PATH" "$(dirname "$0")/InstaCodeUNA.pro"


# Compilar
echo "🔨 Compilando..."
make

# Permitir ejecución
chmod +x InstaCodeUNA

# Ejecutar
echo "🚀 Ejecutando aplicación..."
./InstaCodeUNA
