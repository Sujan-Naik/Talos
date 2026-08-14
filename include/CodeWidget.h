#ifndef TALOS_CODEWIDGET_H
#define TALOS_CODEWIDGET_H
#include "ChatWidget.h"


class CodeWidget: public QWidget {
    Q_OBJECT

public:
    explicit CodeWidget(QWidget *parent = nullptr);

    ~CodeWidget() override = default;
};


#endif //TALOS_CODEWIDGET_H
