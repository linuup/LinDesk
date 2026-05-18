#pragma once

#include <QLineEdit>
#include "app/AppFonts.h"

class FluentInput : public QLineEdit {
public:
    explicit FluentInput(QWidget* parent = nullptr)
        : QLineEdit(parent)
    {
        setMinimumHeight(36);
        setFont(AppFonts::uiFont(10));
        setClearButtonEnabled(true);
        setStyleSheet(
            "QLineEdit {"
            "  background-color: #ffffff;"
            "  border: 1px solid #d6dfec;"
            "  border-radius: 6px;"
            "  padding: 7px 10px;"
            "  color: #142033;"
            "  selection-background-color: #0078d4;"
            "  selection-color: #ffffff;"
            "}"
            "QLineEdit:hover {"
            "  border-color: #b8c7dc;"
            "}"
            "QLineEdit:focus {"
            "  border: 1px solid #0078d4;"
            "}"
            "QLineEdit:disabled {"
            "  background-color: #f3f4f6;"
            "  color: #8a94a6;"
            "}");
    }
};
