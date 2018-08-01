//  put "#pragma once" at the top of header files to protect against being included multiple times
#pragma once

#include <QtWidgets>

class KeyboardControlFilter : public QObject {
	public:
		KeyboardControlFilter(QObject* parent) : QObject(parent) {}

	protected:
	    bool eventFilter(QObject *obj, QEvent *event);

	private:
		int keyPressed;
};