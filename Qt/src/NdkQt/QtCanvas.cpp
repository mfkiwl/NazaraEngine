// Copyright (C) 2016 Jérôme Leclercq
// This file is part of the "Nazara Development Kit Qt Layer"
// For conditions of distribution and use, see copyright notice in Prerequesites.hpp

#include <NdkQt/QtCanvas.hpp>
#include <QtGui/QKeyEvent>
#include <iostream>

namespace Ndk
{
	QtCanvas::QtCanvas(QWidget* parent) :
	QWidget(parent)
	{
		// Setup some states to allow direct rendering into the widget
		setAttribute(Qt::WA_PaintOnScreen);
		setAttribute(Qt::WA_OpaquePaintEvent);
		setAttribute(Qt::WA_NoSystemBackground);

		// Set strong focus to enable keyboard events to be received
		setFocusPolicy(Qt::StrongFocus);
	}

	unsigned int QtCanvas::GetHeight() const
	{
		return height();
	}

	unsigned int QtCanvas::GetWidth() const
	{
		return width();
	}

	void QtCanvas::resizeEvent(QResizeEvent*)
	{
		OnRenderTargetSizeChange(this);
	}

	void QtCanvas::showEvent(QShowEvent*)
	{
		if (!IsValid())
		{
			#ifdef Q_WS_X11
			XFlush(QX11Info::display());
			#endif

			Nz::RenderWindow::Create(reinterpret_cast<Nz::WindowHandle>(winId()));
		}
	}

	QPaintEngine* QtCanvas::paintEngine() const
	{
		return nullptr;
	}

	void QtCanvas::paintEvent(QPaintEvent*)
	{
	}

	void QtCanvas::keyPressEvent(QKeyEvent* key)
	{
		std::cout << (char) key->key() << std::endl;
	}
}
