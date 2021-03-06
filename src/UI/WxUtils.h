#pragma once

#include "General/UI.h"

namespace WxUtils
{
wxMenuItem* createMenuItem(
	wxMenu*         menu,
	int             id,
	const wxString& label,
	const wxString& help = wxEmptyString,
	const wxString& icon = wxEmptyString);
wxFont       monospaceFont(wxFont base);
wxImageList* createSmallImageList();
wxPanel*     createPadPanel(wxWindow* parent, wxWindow* control, int pad = -1);

wxSizer* createLabelHBox(wxWindow* parent, const wxString& label, wxWindow* widget);
wxSizer* createLabelHBox(wxWindow* parent, const wxString& label, wxSizer* sizer);
wxSizer* createLabelVBox(wxWindow* parent, const wxString& label, wxWindow* widget);
wxSizer* createLabelVBox(wxWindow* parent, const wxString& label, wxSizer* sizer);

wxSizer* layoutHorizontally(vector<wxObject*> widgets, int expand_col = -1);
void     layoutHorizontally(wxSizer* sizer, vector<wxObject*> widgets, wxSizerFlags flags = {}, int expand_col = -1);

wxSizer* layoutVertically(vector<wxObject*> widgets, int expand_row = -1);
void     layoutVertically(wxSizer* sizer, vector<wxObject*> widgets, wxSizerFlags flags = {}, int expand_row = -1);

// Strings
inline std::string_view strToView(const wxString& str)
{
	return { str.data(), str.size() };
}
inline wxString strFromView(std::string_view view)
{
	return { view.data(), view.size() };
}
wxArrayString arrayString(vector<wxString> vector);
wxArrayString arrayStringStd(vector<std::string> vector);

// Scaling
wxSize  scaledSize(int x, int y);
wxPoint scaledPoint(int x, int y);
wxRect  scaledRect(int x, int y, int width, int height);
} // namespace WxUtils
