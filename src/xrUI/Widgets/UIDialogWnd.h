#pragma once

#include "UIWindow.h"
#include "UIDialogHolder.h"

class UI_API CUIDialogWnd : public CUIWindow  
{
private:
	typedef CUIWindow inherited;
	CDialogHolder*					m_pParentHolder;
	bool m_allowMovement = false;
	bool m_needCursor = true;
	bool m_centerCursor = true;
protected:
public:
	bool										m_bWorkInPause;
				CUIDialogWnd					();
	virtual		~CUIDialogWnd					();

	virtual void Show							(bool status);

	virtual bool OnKeyboardAction						(int dik, EUIMessages keyboard_action);
	virtual bool OnKeyboardHold					(int dik);

	CDialogHolder* GetHolder					()								{return m_pParentHolder;};
			void SetHolder						(CDialogHolder* h)				{m_pParentHolder = h;};
	void AllowMovement(bool value) { m_allowMovement = value; }
	void AllowCursor(bool value) { m_needCursor = value; }
	void AllowCenterCursor(bool value) { m_centerCursor = value; }
	void AllowWorkInPause(bool value) { m_bWorkInPause = value; }
	virtual bool StopAnyMove() { return !m_allowMovement; }
	virtual bool NeedCursor() const { return m_needCursor; }
	virtual bool NeedCenterCursor() const { return m_centerCursor; }
	virtual bool WorkInPause					()const							{return m_bWorkInPause;}
	virtual bool Dispatch						(int cmd, int param)			{return true;}
    virtual void ShowOrHideDialog				(bool bDoHideIndicators);
			void ShowDialog						(bool bDoHideIndicators);
			void HideDialog						();

	virtual bool IR_process						();

	virtual CUIWindow* ui_cast_window() { return this; }
};
