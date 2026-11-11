#include "framework.h"
#include "VideoPlayerApp.h"
#include "VideoPlayerDoc.h"
#include "VideoPlayerView.h"

#include "Interface.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

IMPLEMENT_DYNCREATE(CVideoPlayerView, CView)

BEGIN_MESSAGE_MAP(CVideoPlayerView, CView)
	ON_WM_CREATE()
	ON_WM_SIZE()
END_MESSAGE_MAP()

CVideoPlayerView::CVideoPlayerView() noexcept
{
}

CVideoPlayerView::~CVideoPlayerView()
{
}

BOOL CVideoPlayerView::PreCreateWindow(CREATESTRUCT& cs)
{
	return CView::PreCreateWindow(cs);
}

void CVideoPlayerView::OnDraw(CDC* /*pDC*/)
{
	CVideoPlayerDoc* pDoc = GetDocument();
	ASSERT_VALID(pDoc);
	if (!pDoc)
		return;

	// TODO: add draw code for native data here
}

#ifdef _DEBUG
void CVideoPlayerView::AssertValid() const
{
	CView::AssertValid();
}

void CVideoPlayerView::Dump(CDumpContext& dc) const
{
	CView::Dump(dc);
}

CVideoPlayerDoc* CVideoPlayerView::GetDocument() const // non-debug version is inline
{
	ASSERT(m_pDocument->IsKindOf(RUNTIME_CLASS(CVideoPlayerDoc)));
	return (CVideoPlayerDoc*)m_pDocument;
}
#endif //_DEBUG

int CVideoPlayerView::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CView::OnCreate(lpCreateStruct) == -1)
		return -1;

	HRESULT hr = CreatePlayerInstance(GetSafeHwnd(), NULL, &m_playerInstance);

	if (FAILED(hr))
	{
		AfxMessageBox(_T("Failed to create player instance."));
		return -1;
    }

	return 0;
}

void CVideoPlayerView::OnSize(UINT nType, int cx, int cy)
{
	CView::OnSize(nType, cx, cy);

	if (m_playerInstance == 0)
		return;

	RECT newWindowPos;
	newWindowPos.left = 0;
	newWindowPos.top = 0;
	newWindowPos.right = cx;
	newWindowPos.bottom = cy;
	HRESULT hr = SetPlayerWindowPos(m_playerInstance, newWindowPos);

	if (FAILED(hr))
	{
		AfxMessageBox(_T("Failed to set player window position."));
    }
}
