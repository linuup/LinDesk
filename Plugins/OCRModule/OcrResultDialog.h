#pragma once
#include "../../src/ui/Fluent/FluentResultView.h"

class OcrResultDialog : public FluentResultView {
    Q_OBJECT
public:
    explicit OcrResultDialog(QWidget* parent = nullptr) 
        : FluentResultView("Recognized Text", u8"\uEBD2", parent) 
    {
        setWindowTitle("OCR Intelligence Result");
    }

    // Reuse setResultText from base class
};
