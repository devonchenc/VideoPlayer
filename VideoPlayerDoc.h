#pragma once

class CVideoPlayerDoc : public CDocument
{
protected: // create from serialization only
	CVideoPlayerDoc() noexcept;
	DECLARE_DYNCREATE(CVideoPlayerDoc)

public:

public:
	virtual BOOL OnNewDocument();
	virtual void Serialize(CArchive& ar);

public:
	virtual ~CVideoPlayerDoc();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

protected:

protected:
	DECLARE_MESSAGE_MAP()
};
