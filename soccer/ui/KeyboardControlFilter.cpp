
#include "KeyPressEater.hpp"

bool KeyboardControlFilter::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        qDebug("Ate key press %d", keyEvent->key());
    	keyPressed = keyEvent->key();    
        return true;
    } else {
        // standard event processing
        return QObject::eventFilter(obj, event);
    }
}

char KeyboardControlFilter::getKeyPressed()
{	
	if (keyPresesd == Qt::Key_I) {
		return 'I'
	}
	return 'K';
}