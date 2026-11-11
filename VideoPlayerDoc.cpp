#include "framework.h"
#include "VideoPlayerApp.h"
#include "VideoPlayerDoc.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

IMPLEMENT_DYNCREATE(CVideoPlayerDoc, CDocument)

BEGIN_MESSAGE_MAP(CVideoPlayerDoc, CDocument)
END_MESSAGE_MAP()

CVideoPlayerDoc::CVideoPlayerDoc() noexcept
{
}

CVideoPlayerDoc::~CVideoPlayerDoc()
{
}

BOOL CVideoPlayerDoc::OnNewDocument()
{
	if (!CDocument::OnNewDocument())
		return FALSE;

	return TRUE;
}

void CVideoPlayerDoc::Serialize(CArchive& ar)
{
	if (ar.IsStoring())
	{
		// TODO: add storing code here
	}
	else
	{
		// TODO: add loading code here
	}
}

#ifdef _DEBUG
void CVideoPlayerDoc::AssertValid() const
{
	CDocument::AssertValid();
}

void CVideoPlayerDoc::Dump(CDumpContext& dc) const
{
	CDocument::Dump(dc);
}
#endif //_DEBUG
