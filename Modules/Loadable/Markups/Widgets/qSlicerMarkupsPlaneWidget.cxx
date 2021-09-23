/*==============================================================================

  Program: 3D Slicer

  Copyright (c) Kitware Inc.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  This file was originally developed by Kyle Sunderland, PerkLab, Queen's University
  and was supported through CANARIE's Research Software Program, Cancer
  Care Ontario, OpenAnatomy, and Brigham and Women's Hospital through NIH grant R01MH112748.

  ==============================================================================*/

// Markups widgets includes
#include "qSlicerMarkupsAdditionalOptionsWidget_p.h"

// qMRML includes
#include "qSlicerMarkupsPlaneWidget.h"
#include "ui_qSlicerMarkupsPlaneWidget.h"

// MRML includes
#include <vtkMRMLMarkupsPlaneNode.h>

// STD includes
#include <vector>

// --------------------------------------------------------------------------
class qSlicerMarkupsPlaneWidgetPrivate:
  public qSlicerMarkupsAdditionalOptionsWidgetPrivate,
  public Ui_qSlicerMarkupsPlaneWidget
{
  Q_DECLARE_PUBLIC(qSlicerMarkupsPlaneWidget);
protected:
  qSlicerMarkupsPlaneWidget* const q_ptr;
public:

  qSlicerMarkupsPlaneWidgetPrivate(qSlicerMarkupsPlaneWidget* object);
  void setupUi(qSlicerMarkupsPlaneWidget* widget);
};

// --------------------------------------------------------------------------
qSlicerMarkupsPlaneWidgetPrivate::qSlicerMarkupsPlaneWidgetPrivate(qSlicerMarkupsPlaneWidget* object)
  : q_ptr(object)
{
}

// --------------------------------------------------------------------------
void qSlicerMarkupsPlaneWidgetPrivate::setupUi(qSlicerMarkupsPlaneWidget* widget)
{
  Q_Q(qSlicerMarkupsPlaneWidget);

  this->Ui_qSlicerMarkupsPlaneWidget::setupUi(widget);

  this->planeTypeComboBox->clear();
  for (int planeType = 0; planeType < vtkMRMLMarkupsPlaneNode::PlaneType_Last; ++planeType)
    {
    this->planeTypeComboBox->addItem(vtkMRMLMarkupsPlaneNode::GetPlaneTypeAsString(planeType), planeType);
    }

  this->planeSizeModeComboBox->clear();
  for (int sizeMode = 0; sizeMode < vtkMRMLMarkupsPlaneNode::SizeMode_Last; ++sizeMode)
    {
    this->planeSizeModeComboBox->addItem(vtkMRMLMarkupsPlaneNode::GetSizeModeAsString(sizeMode), sizeMode);
    }

  QObject::connect(this->planeTypeComboBox, SIGNAL(currentIndexChanged(int)),
                   q, SLOT(onPlaneTypeIndexChanged()));
  QObject::connect(this->planeSizeModeComboBox, SIGNAL(currentIndexChanged(int)),
    q, SLOT(onPlaneSizeModeIndexChanged()));
  QObject::connect(this->sizeXSpinBox, SIGNAL(valueChanged(double)),
    q, SLOT(onPlaneSizeSpinBoxChanged()));
  QObject::connect(this->sizeYSpinBox, SIGNAL(valueChanged(double)),
    q, SLOT(onPlaneSizeSpinBoxChanged()));

  q->setEnabled(vtkMRMLMarkupsPlaneNode::SafeDownCast(this->MarkupsNode) != nullptr);
  q->setVisible(vtkMRMLMarkupsPlaneNode::SafeDownCast(this->MarkupsNode) != nullptr);
}

// --------------------------------------------------------------------------
// qSlicerMarkupsPlaneWidget methods

// --------------------------------------------------------------------------
qSlicerMarkupsPlaneWidget::
qSlicerMarkupsPlaneWidget(QWidget* parent)
  : Superclass(*new qSlicerMarkupsPlaneWidgetPrivate(this), parent)
{
  this->setup();
}

// --------------------------------------------------------------------------
qSlicerMarkupsPlaneWidget::
qSlicerMarkupsPlaneWidget(qSlicerMarkupsPlaneWidgetPrivate &d, QWidget* parent)
  : Superclass(d, parent)
{
  this->setup();
}

// --------------------------------------------------------------------------
qSlicerMarkupsPlaneWidget::~qSlicerMarkupsPlaneWidget() = default;

// --------------------------------------------------------------------------
void qSlicerMarkupsPlaneWidget::setup()
{
  Q_D(qSlicerMarkupsPlaneWidget);
  d->setupUi(this);
}

// --------------------------------------------------------------------------
vtkMRMLMarkupsPlaneNode* qSlicerMarkupsPlaneWidget::mrmlPlaneNode()const
{
  Q_D(const qSlicerMarkupsPlaneWidget);
  return vtkMRMLMarkupsPlaneNode::SafeDownCast(d->MarkupsNode);
}

// --------------------------------------------------------------------------
void qSlicerMarkupsPlaneWidget::setMRMLMarkupsNode(vtkMRMLMarkupsNode* markupsNode)
{
  Q_D(qSlicerMarkupsPlaneWidget);

  Superclass::setMRMLMarkupsNode(markupsNode);
}

// --------------------------------------------------------------------------
void qSlicerMarkupsPlaneWidget::setMRMLMarkupsNode(vtkMRMLNode* node)
{
  this->setMRMLMarkupsNode(vtkMRMLMarkupsPlaneNode::SafeDownCast(node));
}

// --------------------------------------------------------------------------
void qSlicerMarkupsPlaneWidget::updateWidgetFromMRML()
{
  Q_D(qSlicerMarkupsPlaneWidget);

  this->setEnabled(this->canManageMRMLMarkupsNode(d->MarkupsNode));
  this->setVisible(this->canManageMRMLMarkupsNode(d->MarkupsNode));

  vtkMRMLMarkupsPlaneNode* planeNode = vtkMRMLMarkupsPlaneNode::SafeDownCast(d->MarkupsNode);
  if (!planeNode)
    {
    return;
    }

  bool wasBlocked = d->planeTypeComboBox->blockSignals(true);
  d->planeTypeComboBox->setCurrentIndex(d->planeTypeComboBox->findData(planeNode->GetPlaneType()));
  d->planeTypeComboBox->blockSignals(wasBlocked);

  wasBlocked = d->planeSizeModeComboBox->blockSignals(true);
  d->planeSizeModeComboBox->setCurrentIndex(d->planeSizeModeComboBox->findData(planeNode->GetSizeMode()));
  d->planeSizeModeComboBox->blockSignals(wasBlocked);

  double* size = planeNode->GetSize();

  wasBlocked = d->sizeXSpinBox->blockSignals(true);
  d->sizeXSpinBox->setValue(size[0]);
  d->sizeXSpinBox->setMaximum(vtkMath::Max(d->sizeXSpinBox->maximum(), size[0]));
  d->sizeXSpinBox->blockSignals(wasBlocked);
  d->sizeXSpinBox->setEnabled(planeNode->GetSizeMode() != vtkMRMLMarkupsPlaneNode::SizeModeAuto);

  wasBlocked = d->sizeYSpinBox->blockSignals(true);
  d->sizeYSpinBox->setValue(size[1]);
  d->sizeYSpinBox->setMaximum(vtkMath::Max(d->sizeYSpinBox->maximum(), size[1]));
  d->sizeYSpinBox->blockSignals(wasBlocked);
  d->sizeYSpinBox->setEnabled(planeNode->GetSizeMode() != vtkMRMLMarkupsPlaneNode::SizeModeAuto);
}

//-----------------------------------------------------------------------------
void qSlicerMarkupsPlaneWidget::onPlaneTypeIndexChanged()
{
  Q_D(qSlicerMarkupsPlaneWidget);
  vtkMRMLMarkupsPlaneNode* planeNode = vtkMRMLMarkupsPlaneNode::SafeDownCast(d->MarkupsNode);
  if (!planeNode)
    {
    return;
    }
  MRMLNodeModifyBlocker blocker(planeNode);
  planeNode->SetPlaneType(d->planeTypeComboBox->currentData().toInt());
}

//-----------------------------------------------------------------------------
void qSlicerMarkupsPlaneWidget::onPlaneSizeModeIndexChanged()
{
  Q_D(qSlicerMarkupsPlaneWidget);
  vtkMRMLMarkupsPlaneNode* planeNode = vtkMRMLMarkupsPlaneNode::SafeDownCast(d->MarkupsNode);
  if (!planeNode)
    {
    return;
    }
  MRMLNodeModifyBlocker blocker(planeNode);
  planeNode->SetSizeMode(d->planeSizeModeComboBox->currentData().toInt());
}

//-----------------------------------------------------------------------------
void qSlicerMarkupsPlaneWidget::onPlaneSizeSpinBoxChanged()
{
  Q_D(qSlicerMarkupsPlaneWidget);
  vtkMRMLMarkupsPlaneNode* planeNode = vtkMRMLMarkupsPlaneNode::SafeDownCast(d->MarkupsNode);
  if (!planeNode)
    {
    return;
    }
  MRMLNodeModifyBlocker blocker(planeNode);
  planeNode->SetSize(d->sizeXSpinBox->value(), d->sizeYSpinBox->value());
}

//-----------------------------------------------------------------------------
bool qSlicerMarkupsPlaneWidget::canManageMRMLMarkupsNode(vtkMRMLMarkupsNode *markupsNode) const
{
  Q_D(const qSlicerMarkupsPlaneWidget);

  vtkMRMLMarkupsPlaneNode* planeNode = vtkMRMLMarkupsPlaneNode::SafeDownCast(markupsNode);
  if (!planeNode)
    {
    return false;
    }

  return true;
}
