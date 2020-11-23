/*==============================================================================

  Program: 3D Slicer

  Portions (c) Copyright Brigham and Women's Hospital (BWH) All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

==============================================================================*/

#ifndef __qSlicerAbstractMarkupsSettingsWidget_h
#define __qSlicerAbstractMarkupsSettingsWidget_h

// CTK includes
#include <ctkVTKObject.h>
#include <ctkSettingsPanel.h>

// Markups includes
#include "qSlicerMarkupsModuleExport.h"
class qSlicerAbstractMarkupsSettingsWidgetPrivate;
class vtkSlicerMarkupsLogic;

#include <qMRMLWidget.h>

class Q_SLICER_QTMODULES_MARKUPS_EXPORT qSlicerAbstractMarkupsSettingsWidget
  : public qMRMLWidget
{
  Q_OBJECT
  QVTK_OBJECT

public:
  /// Superclass typedef
  typedef qMRMLWidget Superclass;

  /// Constructor
  explicit qSlicerAbstractMarkupsSettingsWidget(QWidget* parent = nullptr);

  /// Destructor
  ~qSlicerAbstractMarkupsSettingsWidget() override;

  virtual QStringList supportedClassNames() const;

private:
  Q_DISABLE_COPY(qSlicerAbstractMarkupsSettingsWidget);
};

#endif
