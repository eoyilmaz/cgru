#include "listwork.h"

#include "../include/afanasy.h"

#include "../libafanasy/address.h"
#include "../libafanasy/environment.h"
#include "../libafanasy/monitor.h"
#include "../libafanasy/monitorevents.h"
#include "../libafanasy/msgclasses/mctaskpos.h"

#include "actionid.h"
#include "dialog.h"
#include "buttonpanel.h"
#include "ctrlwork.h"
#include "ctrlsortfilter.h"
#include "itemjob.h"
#include "itembranch.h"
#include "modelnodes.h"
#include "paramspanelfarm.h"
#include "viewitems.h"
#include "watch.h"
#include "wndtask.h"

#include <QtCore/QEvent>
#include <QtCore/QTimer>
#include <QtGui/QContextMenuEvent>
#include <QInputDialog>
#include <QLabel>
#include <QLayout>
#include <QMenu>

#define AFOUTPUT
#undef AFOUTPUT
#include "../include/macrooutput.h"
#include "../libafanasy/logger.h"

ListWork::EDisplaySize ListWork::ms_displaysize = ListWork::EVariableSize;

int     ListWork::ms_SortType1      = CtrlSortFilter::TTIMECREATION;
int     ListWork::ms_SortType2      = CtrlSortFilter::TTIMERUN;
bool    ListWork::ms_SortAscending1 = false;
bool    ListWork::ms_SortAscending2 = false;
int     ListWork::ms_FilterType     = CtrlSortFilter::TUSERNAME;
bool    ListWork::ms_FilterInclude  = true;
bool    ListWork::ms_FilterMatch    = false;
std::string ListWork::ms_FilterString = "";

uint32_t ListWork::ms_hide_flags = e_HideHidden | e_HideSystem | e_HideDone | e_HideEmpty;

ListWork::ListWork(QWidget* parent):
	ListNodes(parent, "jobs")
{
	m_node_types.clear();
	m_node_types.push_back("branches");
	m_node_types.push_back("jobs");

	m_ctrl_sf = new CtrlSortFilter(this,
			&ms_SortType1, &ms_SortAscending1,
			&ms_SortType2, &ms_SortAscending2,
			&ms_FilterType, &ms_FilterInclude, &ms_FilterMatch, &ms_FilterString
		);

	m_ctrl_sf->addSortType(  CtrlSortFilter::TNONE);
	m_ctrl_sf->addSortType(  CtrlSortFilter::TTIMECREATION);
	m_ctrl_sf->addSortType(  CtrlSortFilter::TTIMERUN);
	m_ctrl_sf->addSortType(  CtrlSortFilter::TTIMESTARTED);
	m_ctrl_sf->addSortType(  CtrlSortFilter::TTIMEFINISHED);
	m_ctrl_sf->addSortType(  CtrlSortFilter::TNUMRUNNINGTASKS);
	m_ctrl_sf->addSortType(  CtrlSortFilter::TSERVICE);
	m_ctrl_sf->addSortType(  CtrlSortFilter::TNAME);
	m_ctrl_sf->addFilterType(CtrlSortFilter::TNONE);
	m_ctrl_sf->addFilterType(CtrlSortFilter::TNAME);
	m_ctrl_sf->addFilterType(CtrlSortFilter::TSERVICE);
	m_ctrl_sf->addSortType(  CtrlSortFilter::TPRIORITY);
	m_ctrl_sf->addSortType(  CtrlSortFilter::THOSTNAME);
	m_ctrl_sf->addSortType(  CtrlSortFilter::TUSERNAME);
	m_ctrl_sf->addFilterType(CtrlSortFilter::THOSTNAME);
	m_ctrl_sf->addFilterType(CtrlSortFilter::TUSERNAME);

	// Get stored hide flags:
	m_hide_flags = ms_hide_flags;

	initSortFilterCtrl();

	CtrlWork * control = new CtrlWork(m_ctrl_sf, this);
	m_ctrl_sf->getLayout()->addWidget(control);

	// Add left panel buttons:
	ButtonPanel * bp; ButtonsMenu * bm;

	bp = addButtonPanel(Item::TJob, "LOG","jobs_log","Show job log.");
	connect(bp, SIGNAL(sigClicked()), this, SLOT(slot_RequestLog()));

	bp = addButtonPanel(Item::TJob, "PAUSE","jobs_pause","Pause selected jobs.","P");
	connect(bp, SIGNAL(sigClicked()), this, SLOT(slot_Pause()));

	bp = addButtonPanel(Item::TJob, "START","jobs_start","Start selected jobs.","S");
	connect(bp, SIGNAL(sigClicked()), this, SLOT(slot_Start()));

	bp = addButtonPanel(Item::TJob, "STOP","jobs_stop","Stop selected jobs tasks and pause jobs.","", true);
	connect(bp, SIGNAL(sigClicked()), this, SLOT(slot_Stop()));


	m_parentWindow->setWindowTitle("Jobs");

	addParam_Num(Item::TAny,    "priority",                  "Priority",             "Priority number", 0, 250);
	addParam_Str(Item::TAny,    "annotation",                "Annotation",           "Annotation string");
	addParam_Num(Item::TAny,    "max_running_tasks",         "Maximum Running",      "Maximum running tasks number", -1, 1<<20);
	addParam_Num(Item::TAny,    "max_running_tasks_per_host","Max Run Per Host",     "Max run tasks on the same host", -1, 1<<20);
	addParam_REx(Item::TAny,    "hosts_mask",                "Hosts Mask",           "Host names pattern that job can run on");
	addParam_REx(Item::TAny,    "hosts_mask_exclude",        "Hosts Mask Exclude",   "Host names pattern that job will not run");
	addParam_Num(Item::TBranch, "max_tasks_per_second",      "Max Tasks Per Second", "Maximum tasks starts per second", -1, 1<<20);

	initListNodes();

	setSpacing();

	QTimer * timer = new QTimer(this);
	timer->start(1900 * af::Environment::getWatchRefreshGuiSec());
	connect(timer, SIGNAL(timeout()), this, SLOT(repaintItems()));
}

ListWork::~ListWork()
{
	// Store hide flags:
	ms_hide_flags = m_hide_flags;
}

void ListWork::setSpacing()
{
	 switch(ms_displaysize)
	 {
	 case  ListWork::ESmallSize:
		  m_view->setSpacing(1);
		  break;
	 case  ListWork::ENormalSize:
		  m_view->setSpacing(2);
		  break;
	 default:
		  m_view->setSpacing(3);
	 }
}

void ListWork::slot_ChangeSize(int i_size)
{
	 ListWork::EDisplaySize dsize = (ListWork::EDisplaySize)i_size;

	 if (dsize == ms_displaysize)
		  return;

	 ms_displaysize = dsize;

	 setSpacing();
	 itemsHeightCalc();
	 revertModel();
	 repaintItems();
}

void ListWork::contextMenuEvent(QContextMenuEvent *event)
{
	Item * item = getCurrentItem();
	if (item == NULL)
		return;

	if (item->getType() != Item::TJob)
		return;

	ItemJob * job = (ItemJob*)item;

	QMenu menu(this);
	QAction *action;

	action = new QAction("Show Log", this);
	connect(action, SIGNAL(triggered()), this, SLOT(slot_RequestLog()));
	menu.addAction(action);

	menu.addSeparator();

/*
	action = new QAction("Annotate", this);
	connect(action, SIGNAL(triggered()), this, SLOT(slot_Annotate()));
	submenu->addAction(action);
	action = new QAction("Set Priority", this);
	connect(action, SIGNAL(triggered()), this, SLOT(slot_Priority()));
	submenu->addAction(action);

	submenu->addSeparator();

	action = new QAction("Set Paused", this);
	connect(action, SIGNAL(triggered()), this, SLOT(slot_SetPaused()));
	submenu->addAction(action);
	action = new QAction("Unset Paused", this);
	connect(action, SIGNAL(triggered()), this, SLOT(slot_UnsetPaused()));
	submenu->addAction(action);

	submenu->addSeparator();

	action = new QAction("Change Max Tasks", this);
	connect(action, SIGNAL(triggered()), this, SLOT(slot_MaxTasks()));
	submenu->addAction(action);
*/

	menu.exec(event->globalPos());
}

bool ListWork::v_caseMessage(af::Msg * msg)
{
#ifdef AFOUTPUT
	msg->stdOut();
#endif
	switch(msg->type())
	{
	case af::Msg::TBranchesList:
	{
		updateItems(msg, Item::TBranch);
		if (false == isSubscribed())
			get("jobs");
		calcTitle();
		break;
	}
	case af::Msg::TJobsList:
	{
		subscribe();
		updateItems(msg, Item::TJob);
		calcTitle();
		break;
	}
	default:
		return false;
	}
	return true;
}

bool ListWork::v_processEvents(const af::MonitorEvents & i_me)
{
	bool processed = false;

	// Delete jobs by ids:
	if (i_me.m_events[af::Monitor::EVT_jobs_del].size())
	{
		deleteItems(i_me.m_events[af::Monitor::EVT_jobs_del], Item::TJob);
		calcTitle();
		processed = true;
	}

	// Delete branches by ids:
	if (i_me.m_events[af::Monitor::EVT_branches_del].size())
	{
		deleteItems(i_me.m_events[af::Monitor::EVT_branches_del], Item::TBranch);
		calcTitle();
		processed = true;
	}

	// Get new and changed branches ids:
	std::vector<int> pids;
	for (int i = 0; i < i_me.m_events[af::Monitor::EVT_branches_change].size(); i++)
		af::addUniqueToVect(pids, i_me.m_events[af::Monitor::EVT_branches_change][i]);
	for (int i = 0; i < i_me.m_events[af::Monitor::EVT_branches_add].size(); i++)
		af::addUniqueToVect(pids, i_me.m_events[af::Monitor::EVT_branches_add][i]);
	if (pids.size())
	{
		get(pids, "branches");
		processed = true;
	}

	// Get new and changed jobs ids:
	std::vector<int> rids;
	for (int i = 0; i < i_me.m_events[af::Monitor::EVT_jobs_change].size(); i++)
		af::addUniqueToVect(rids, i_me.m_events[af::Monitor::EVT_jobs_change][i]);
	for (int i = 0; i < i_me.m_events[af::Monitor::EVT_jobs_add].size(); i++)
		af::addUniqueToVect(rids, i_me.m_events[af::Monitor::EVT_jobs_add][i]);
	if (rids.size())
	{
		get(rids, "jobs");
		processed = true;
	}

	return processed;
}

ItemNode * ListWork::v_createNewItemNode(af::Node * i_afnode, Item::EType i_type, bool i_notify)
{
	switch (i_type)
	{
	case Item::TJob:
		return new ItemJob(this, true /*in work list*/, (af::Job*)i_afnode, m_ctrl_sf, i_notify);
	case Item::TBranch:
		return new ItemBranch(this, (af::Branch*)i_afnode, m_ctrl_sf);
	default:
		AF_ERR << "Invalid Item::EType: " << i_type;
		return NULL;
	}
}

bool ListWork::v_filesReceived(const af::MCTaskUp & i_taskup)
{
	if ((i_taskup.getNumBlock() != -1) || (i_taskup.getNumTask() != -1))
		return false; // This is for a task (not for an entire job)

	for (int i = 0; i < count(); i++)
	{
		Item * item = (ItemJob*)(m_model->item(i));
		if (NULL == item)
			continue;
		if (item->getType() != Item::TJob)
			continue;
		if (item->getId() != i_taskup.getNumJob())
			continue;

		ItemJob * itemjob = static_cast<ItemJob*>(item);
		itemjob->v_filesReceived(i_taskup);
		return true;
	}

	return false;
}


void ListWork::calcTitle()
{
	m_parentWindow->setWindowTitle(QString("Count: %1").arg(count()));
}

void ListWork::slot_JobSetBranch()
{
	Item * item = getCurrentItem();
	if (item == NULL)
		return;

	if (item->getType() != Item::TJob)
		return;

	bool ok;
	QString name = QInputDialog::getText(this, "Set Job Branch",
			"Enter job(s) new branch name",
			QLineEdit::Normal, QString(), &ok);
	if (false == ok)
		return;

	displayInfo(QString("Setting branch to \"%1\"").arg(name));

	jobSetBranch(name);
}

void ListWork::jobSetBranch(const QString & i_name)
{
	Item::EType type = Item::TJob;
	std::vector<int> ids(getSelectedIds(type));
	std::ostringstream str;
	af::jsonActionOperationStart(str, "jobs", "set_branch", "", ids);
	str << ",\n\"name\":\"" << afqt::qtos(i_name) << "\"";
	af::jsonActionOperationFinish(str);
	Watch::sendMsg(af::jsonMsg(str));
}

void ListWork::slot_MaxTasks()
{
/*	ItemJob* item = (ItemJob*)getCurrentItem();
	if (item == NULL) return;
	int current = item->getMaxRunningTasks();

	bool ok;
	int max_tasks = QInputDialog::getInt(this, "Change Maximum Tasksy", "Enter New Limit", current, -1, 1000000, 1, &ok);
	if (!ok) return;
	setParameter(Item::TJob, "max_tasks", af::itos(max_tasks));
*/
}

void ListWork::slot_Delete() { operation(Item::TAny, "delete"); }

void ListWork::slot_RequestLog() { getItemInfo(Item::TAny, "log"); }
