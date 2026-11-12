#pragma once

class CVideoPlayerView : public CView
{
protected:
	CVideoPlayerView() noexcept;
	DECLARE_DYNCREATE(CVideoPlayerView)

public:
	CVideoPlayerDoc* GetDocument() const;

public:

public:
	virtual void OnDraw(CDC* pDC);
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
protected:

public:
	virtual ~CVideoPlayerView();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

protected:

protected:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnButtonPlay();
	afx_msg void OnButtonPause();
	afx_msg void OnButtonStop();
	DECLARE_MESSAGE_MAP()

private:
	long m_playerInstance{ 0 };
};

#ifndef _DEBUG  // debug version in VideoPlayerView.cpp
inline CVideoPlayerDoc* CVideoPlayerView::GetDocument() const
   { return reinterpret_cast<CVideoPlayerDoc*>(m_pDocument); }
#endif
